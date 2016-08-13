/**
 * @file src/audio.c  Audio stream
 *
 * Copyright (C) 2010 Creytiv.com
 * \ref GenericAudioStream
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


/** Magic number */
#define MAGIC 0x000a0d10
#include "magic.h"


/**
 * \page GenericAudioStream Generic Audio Stream
 *
 * Implements a generic audio stream. The application can allocate multiple
 * instances of a audio stream, mapping it to a particular SDP media line.
 * The audio object has a DSP sound card sink and source, and an audio encoder
 * and decoder. A particular audio object is mapped to a generic media
 * stream object. Each audio channel has an optional audio filtering chain.
 *
 *<pre>
 *            write  read
 *              |    /|\
 *             \|/    |
 * .------.   .---------.    .-------.
 * |filter|<--|  audio  |--->|encoder|
 * '------'   |         |    |-------|
 *            | object  |--->|decoder|
 *            '---------'    '-------'
 *              |    /|\
 *              |     |
 *             \|/    |
 *         .------. .-----.
 *         |auplay| |ausrc|
 *         '------' '-----'
 *</pre>
 */

enum {
	AUDIO_SAMPSZ    = 3*1920  /* Max samples, 48000Hz 2ch at 60ms */
};


/**
 * Audio transmit/encoder
 *
 *
 \verbatim

 Processing encoder pipeline:

 .    .-------.   .-------.   .--------.   .--------.   .--------.
 |    |       |   |       |   |        |   |        |   |        |
 |O-->| ausrc |-->| aubuf |-->| resamp |-->| aufilt |-->| encode |---> RTP
 |    |       |   |       |   |        |   |        |   |        |
 '    '-------'   '-------'   '--------'   '--------'   '--------'

 \endverbatim
 *
 */
struct autx {
	struct ausrc_st *ausrc;       /**< Audio Source                    */
	struct ausrc_prm ausrc_prm;
	const struct aucodec *ac;     /**< Current audio encoder           */
	struct auenc_state *enc;      /**< Audio encoder state (optional)  */
	struct aubuf *aubuf;          /**< Packetize outgoing stream       */
	struct auresamp resamp;       /**< Optional resampler for DSP      */
	struct list filtl;            /**< Audio filters in encoding order */
	struct mbuf *mb;              /**< Buffer for outgoing RTP packets */
	char device[64];              /**< Audio source device name        */
	int16_t *sampv;               /**< Sample buffer                   */
	int16_t *sampv_rs;            /**< Sample buffer for resampler     */
	uint32_t ptime;               /**< Packet time for sending         */
	uint32_t ts;                  /**< Timestamp for outgoing RTP      */
	uint32_t ts_tel;              /**< Timestamp for Telephony Events  */
	size_t psize;                 /**< Packet size for sending         */
	bool marker;                  /**< Marker bit for outgoing RTP     */
	bool muted;                   /**< Audio source is muted           */
	int cur_key;                  /**< Currently transmitted event     */

	union {
		struct tmr tmr;       /**< Timer for sending RTP packets   */
#ifdef HAVE_PTHREAD
		struct {
			pthread_t tid;/**< Audio transmit thread           */
			bool run;     /**< Audio transmit thread running   */
		} thr;
#endif
	} u;
};


/**
 * Audio receive/decoder
 *
 \verbatim

 Processing decoder pipeline:

       .--------.   .-------.   .--------.   .--------.   .--------.
 |\    |        |   |       |   |        |   |        |   |        |
 | |<--| auplay |<--| aubuf |<--| resamp |<--| aufilt |<--| decode |<--- RTP
 |/    |        |   |       |   |        |   |        |   |        |
       '--------'   '-------'   '--------'   '--------'   '--------'

 \endverbatim
 */
struct aurx {
	struct auplay_st *auplay;     /**< Audio Player                    */
	struct auplay_prm auplay_prm;
	const struct aucodec *ac;     /**< Current audio decoder           */
	struct audec_state *dec;      /**< Audio decoder state (optional)  */
	struct aubuf *aubuf;          /**< Incoming audio buffer           */
	struct auresamp resamp;       /**< Optional resampler for DSP      */
	struct list filtl;            /**< Audio filters in decoding order */
	char device[64];              /**< Audio player device name        */
	int16_t *sampv;               /**< Sample buffer                   */
	int16_t *sampv_rs;            /**< Sample buffer for resampler     */
	uint32_t ptime;               /**< Packet time for receiving       */
	int pt;                       /**< Payload type for incoming RTP   */
};


/** Generic Audio stream */
struct audio {
	MAGIC_DECL                    /**< Magic number for debugging      */
	struct autx tx;               /**< Transmit                        */
	struct aurx rx;               /**< Receive                         */
	struct stream *strm;          /**< Generic media stream            */
	struct telev *telev;          /**< Telephony events                */
	struct config_audio cfg;      /**< Audio configuration             */
	bool started;                 /**< Stream is started flag          */
	audio_event_h *eventh;        /**< Event handler                   */
	audio_err_h *errh;            /**< Audio error handler             */
	void *arg;                    /**< Handler argument                */
};


static void stop_tx(struct autx *tx, struct audio *a)
{
	if (!tx || !a)
		return;

	switch (a->cfg.txmode) {

#ifdef HAVE_PTHREAD
	case AUDIO_MODE_THREAD:
	case AUDIO_MODE_THREAD_REALTIME:
		if (tx->u.thr.run) {
			tx->u.thr.run = false;
			pthread_join(tx->u.thr.tid, NULL);
		}
		break;
#endif
	case AUDIO_MODE_TMR:
		tmr_cancel(&tx->u.tmr);
		break;

	default:
		break;
	}

	/* audio source must be stopped first */
	tx->ausrc = mem_deref(tx->ausrc);
	tx->aubuf = mem_deref(tx->aubuf);

	list_flush(&tx->filtl);
}


static void stop_rx(struct aurx *rx)
{
	if (!rx)
		return;

	/* audio player must be stopped first */
	rx->auplay = mem_deref(rx->auplay);
	rx->aubuf  = mem_deref(rx->aubuf);

	list_flush(&rx->filtl);
}


static void audio_destructor(void *arg)
{
	struct audio *a = arg;

	stop_tx(&a->tx, a);
	stop_rx(&a->rx);

	mem_deref(a->tx.enc);
	mem_deref(a->rx.dec);
	mem_deref(a->tx.aubuf);
	mem_deref(a->tx.mb);
	mem_deref(a->tx.sampv);
	mem_deref(a->rx.sampv);
	mem_deref(a->rx.aubuf);
	mem_deref(a->tx.sampv_rs);
	mem_deref(a->rx.sampv_rs);

	list_flush(&a->tx.filtl);
	list_flush(&a->rx.filtl);

	mem_deref(a->strm);
	mem_deref(a->telev);
}


/**
 * Calculate number of samples from sample rate, channels and packet time
 *
 * @param srate    Sample rate in [Hz]
 * @param channels Number of channels
 * @param ptime    Packet time in [ms]
 *
 * @return Number of samples
 */
static inline uint32_t calc_nsamp(uint32_t srate, uint8_t channels,
				  uint16_t ptime)
{
	return srate * channels * ptime / 1000;
}


/**
 * Get the DSP samplerate for an audio-codec (exception for G.722 and MPA)
 */
static inline uint32_t get_srate(const struct aucodec *ac)
{
	if (!ac)
		return 0;

	return ac->srate;
}


/**
 * Get the DSP channels for an audio-codec (exception for MPA)
 */
static inline uint32_t get_ch(const struct aucodec *ac)
{
	if (!ac)
		return 0;

	return !str_casecmp(ac->name, "MPA") ? 2 : ac->ch;
}


static inline uint32_t get_framesize(const struct aucodec *ac,
				     uint32_t ptime)
{
	if (!ac)
		return 0;

	return calc_nsamp(get_srate(ac), get_ch(ac), ptime);
}


static bool aucodec_equal(const struct aucodec *a, const struct aucodec *b)
{
	if (!a || !b)
		return false;

	return get_srate(a) == get_srate(b) && get_ch(a) == get_ch(b);
}


static int add_audio_codec(struct audio *a, struct sdp_media *m,
			   struct aucodec *ac)
{
	if (!in_range(&a->cfg.srate, get_srate(ac))) {
		debug("audio: skip %uHz codec (audio range %uHz - %uHz)\n",
		      get_srate(ac), a->cfg.srate.min, a->cfg.srate.max);
		return 0;
	}

	if (!in_range(&a->cfg.channels, get_ch(ac))) {
		debug("audio: skip codec with %uch (audio range %uch-%uch)\n",
		      get_ch(ac), a->cfg.channels.min, a->cfg.channels.max);
		return 0;
	}

	if (ac->crate < 8000) {
		warning("audio: illegal clock rate %u\n", ac->crate);
		return EINVAL;
	}

	return sdp_format_add(NULL, m, false, ac->pt, ac->name, ac->crate,
			      ac->ch, ac->fmtp_ench, ac->fmtp_cmph, ac, false,
			      "%s", ac->fmtp);
}


/**
 * Encoder audio and send via stream
 *
 * @note This function has REAL-TIME properties
 *
 * @param a     Audio object
 * @param tx    Audio transmit object
 * @param sampv Audio samples
 * @param sampc Number of audio samples
 */
static void encode_rtp_send(struct audio *a, struct autx *tx,
			    int16_t *sampv, size_t sampc)
{
	size_t frame_size;  /* number of samples per channel */
	size_t sampc_rtp;
	size_t len;
	int err;

	if (!tx->ac || !tx->ac->ench)
		return;

	tx->mb->pos = tx->mb->end = STREAM_PRESZ;
	len = mbuf_get_space(tx->mb);

	err = tx->ac->ench(tx->enc, mbuf_buf(tx->mb), &len, sampv, sampc);
	if ((err & 0xffff0000) == 0x00010000) {
		/* MPA needs some special treatment here */
		tx->ts = err & 0xffff;
		err = 0;
	}
	else if (err) {
		warning("audio: %s encode error: %d samples (%m)\n",
			tx->ac->name, sampc, err);
		goto out;
	}

	tx->mb->pos = STREAM_PRESZ;
	tx->mb->end = STREAM_PRESZ + len;

	if (mbuf_get_left(tx->mb)) {
		if (len) {
			err = stream_send(a->strm, tx->marker, -1,
					tx->ts, tx->mb);
			if (err)
				goto out;
		}
	}

	/* Convert from audio samplerate to RTP clockrate */
	sampc_rtp = sampc * tx->ac->crate / tx->ac->srate;

	/* The RTP clock rate used for generating the RTP timestamp is
	 * independent of the number of channels and the encoding
	 * However, MPA support variable packet durations. Thus, MPA
	 * should update the ts according to its current internal state.
	 */
	frame_size = sampc_rtp / get_ch(tx->ac);

	tx->ts += (uint32_t)frame_size;

 out:
	tx->marker = false;
}


/*
 * @note This function has REAL-TIME properties
 */
static void poll_aubuf_tx(struct audio *a)
{
	struct autx *tx = &a->tx;
	int16_t *sampv = tx->sampv;
	size_t sampc;
	struct le *le;
	int err = 0;

	sampc = tx->psize / 2;

	/* timed read from audio-buffer */
	aubuf_read_samp(tx->aubuf, tx->sampv, sampc);

	/* optional resampler */
	if (tx->resamp.resample) {
		size_t sampc_rs = AUDIO_SAMPSZ;

		err = auresamp(&tx->resamp,
			       tx->sampv_rs, &sampc_rs,
			       tx->sampv, sampc);
		if (err)
			return;

		sampv = tx->sampv_rs;
		sampc = sampc_rs;
	}

	/* Process exactly one audio-frame in list order */
	for (le = tx->filtl.head; le; le = le->next) {
		struct aufilt_enc_st *st = le->data;

		if (st->af && st->af->ench)
			err |= st->af->ench(st, sampv, &sampc);
	}
	if (err) {
		warning("audio: aufilter encode: %m\n", err);
	}

	/* Encode and send */
	encode_rtp_send(a, tx, sampv, sampc);
}


static void check_telev(struct audio *a, struct autx *tx)
{
	const struct sdp_format *fmt;
	bool marker = false;
	int err;

	tx->mb->pos = tx->mb->end = STREAM_PRESZ;

	err = telev_poll(a->telev, &marker, tx->mb);
	if (err)
		return;

	if (marker)
		tx->ts_tel = tx->ts;

	fmt = sdp_media_rformat(stream_sdpmedia(audio_strm(a)), telev_rtpfmt);
	if (!fmt)
		return;

	tx->mb->pos = STREAM_PRESZ;
	err = stream_send(a->strm, marker, fmt->pt, tx->ts_tel, tx->mb);
	if (err) {
		warning("audio: telev: stream_send %m\n", err);
	}
}


/**
 * Write samples to Audio Player.
 *
 * @note This function has REAL-TIME properties
 *
 * @note The application is responsible for filling in silence in
 *       the case of underrun
 *
 * @note This function may be called from any thread
 *
 * @param buf Buffer to fill with audio samples
 * @param sz  Number of bytes in buffer
 * @param arg Handler argument
 */
static void auplay_write_handler(int16_t *sampv, size_t sampc, void *arg)
{
	struct aurx *rx = arg;

	aubuf_read_samp(rx->aubuf, sampv, sampc);
}


/**
 * Read samples from Audio Source
 *
 * @note This function has REAL-TIME properties
 *
 * @note This function may be called from any thread
 *
 * @param buf Buffer with audio samples
 * @param sz  Number of bytes in buffer
 * @param arg Handler argument
 */
static void ausrc_read_handler(const int16_t *sampv, size_t sampc, void *arg)
{
	struct audio *a = arg;
	struct autx *tx = &a->tx;

	if (tx->muted)
		memset((void *)sampv, 0, sampc*2);

	(void)aubuf_write_samp(tx->aubuf, sampv, sampc);

	if (a->cfg.txmode == AUDIO_MODE_POLL) {
		unsigned i;

		for (i=0; i<16; i++) {

			if (aubuf_cur_size(tx->aubuf) < tx->psize)
				break;

			poll_aubuf_tx(a);
		}
	}

	/* Exact timing: send Telephony-Events from here */
	check_telev(a, tx);
}


static void ausrc_error_handler(int err, const char *str, void *arg)
{
	struct audio *a = arg;
	MAGIC_CHECK(a);

	if (a->errh)
		a->errh(err, str, a->arg);
}


static int pt_handler(struct audio *a, uint8_t pt_old, uint8_t pt_new)
{
	const struct sdp_format *lc;

	lc = sdp_media_lformat(stream_sdpmedia(a->strm), pt_new);
	if (!lc)
		return ENOENT;

	if (pt_old != (uint8_t)-1) {
		info("Audio decoder changed payload %u -> %u\n",
		     pt_old, pt_new);
	}

	a->rx.pt = pt_new;

	return audio_decoder_set(a, lc->data, lc->pt, lc->params);
}


static void handle_telev(struct audio *a, struct mbuf *mb)
{
	int event, digit;
	bool end;

	if (telev_recv(a->telev, mb, &event, &end))
		return;

	digit = telev_code2digit(event);
	if (digit >= 0 && a->eventh)
		a->eventh(digit, end, a->arg);
}


static int aurx_stream_decode(struct aurx *rx, struct mbuf *mb)
{
	size_t sampc = AUDIO_SAMPSZ;
	int16_t *sampv;
	struct le *le;
	int err = 0;

	/* No decoder set */
	if (!rx->ac)
		return 0;

	if (mbuf_get_left(mb)) {
		err = rx->ac->dech(rx->dec, rx->sampv, &sampc,
				   mbuf_buf(mb), mbuf_get_left(mb));
	}
	else if (rx->ac->plch) {
		sampc = rx->ac->srate * rx->ac->ch * rx->ptime / 1000;

		err = rx->ac->plch(rx->dec, rx->sampv, &sampc);
	}
	else {
		/* no PLC in the codec, might be done in filters below */
		sampc = 0;
	}

	if (err) {
		warning("audio: %s codec decode %u bytes: %m\n",
			rx->ac->name, mbuf_get_left(mb), err);
		goto out;
	}

	/* Process exactly one audio-frame in reverse list order */
	for (le = rx->filtl.tail; le; le = le->prev) {
		struct aufilt_dec_st *st = le->data;

		if (st->af && st->af->dech)
			err |= st->af->dech(st, rx->sampv, &sampc);
	}

	if (!rx->aubuf)
		goto out;

	sampv = rx->sampv;

	/* optional resampler */
	if (rx->resamp.resample) {
		size_t sampc_rs = AUDIO_SAMPSZ;

		err = auresamp(&rx->resamp,
			       rx->sampv_rs, &sampc_rs,
			       rx->sampv, sampc);
		if (err)
			return err;

		sampv = rx->sampv_rs;
		sampc = sampc_rs;
	}

	err = aubuf_write_samp(rx->aubuf, sampv, sampc);
	if (err)
		goto out;

 out:
	return err;
}


/* Handle incoming stream data from the network */
static void stream_recv_handler(const struct rtp_header *hdr,
				struct mbuf *mb, void *arg)
{
	struct audio *a = arg;
	struct aurx *rx = &a->rx;
	int err;

	if (!mb)
		goto out;

	/* Telephone event? */
	if (hdr->pt != rx->pt) {
		const struct sdp_format *fmt;

		fmt = sdp_media_lformat(stream_sdpmedia(a->strm), hdr->pt);

		if (fmt && !str_casecmp(fmt->name, "telephone-event")) {
			handle_telev(a, mb);
			return;
		}
	}

	/* Comfort Noise (CN) as of RFC 3389 */
	if (PT_CN == hdr->pt)
		return;

	/* Audio payload-type changed? */
	/* XXX: this logic should be moved to stream.c */
	if (hdr->pt != rx->pt) {

		err = pt_handler(a, rx->pt, hdr->pt);
		if (err)
			return;
	}

 out:
	(void)aurx_stream_decode(&a->rx, mb);
}


static int add_telev_codec(struct audio *a)
{
	struct sdp_media *m = stream_sdpmedia(audio_strm(a));
	struct sdp_format *sf;
	int err;

	/* Use payload-type 101 if available, for CiscoGW interop */
	err = sdp_format_add(&sf, m, false,
			     (!sdp_media_lformat(m, 101)) ? "101" : NULL,
			     telev_rtpfmt, TELEV_SRATE, 1, NULL,
			     NULL, NULL, false, "0-15");
	if (err)
		return err;

	return err;
}


int audio_alloc(struct audio **ap, const struct config *cfg,
		struct call *call, struct sdp_session *sdp_sess, int label,
		const struct mnat *mnat, struct mnat_sess *mnat_sess,
		const struct menc *menc, struct menc_sess *menc_sess,
		uint32_t ptime, const struct list *aucodecl,
		audio_event_h *eventh, audio_err_h *errh, void *arg)
{
	struct audio *a;
	struct autx *tx;
	struct aurx *rx;
	struct le *le;
	int err;

	if (!ap || !cfg)
		return EINVAL;

	a = mem_zalloc(sizeof(*a), audio_destructor);
	if (!a)
		return ENOMEM;

	MAGIC_INIT(a);

	a->cfg = cfg->audio;
	tx = &a->tx;
	rx = &a->rx;

	err = stream_alloc(&a->strm, &cfg->avt, call, sdp_sess,
			   "audio", label,
			   mnat, mnat_sess, menc, menc_sess,
			   call_localuri(call),
			   stream_recv_handler, NULL, a);
	if (err)
		goto out;

	if (cfg->avt.rtp_bw.max) {
		stream_set_bw(a->strm, AUDIO_BANDWIDTH);
	}

	err = sdp_media_set_lattr(stream_sdpmedia(a->strm), true,
				  "ptime", "%u", ptime);
	if (err)
		goto out;

	/* Audio codecs */
	for (le = list_head(aucodecl); le; le = le->next) {
		err = add_audio_codec(a, stream_sdpmedia(a->strm), le->data);
		if (err)
			goto out;
	}

	tx->mb = mbuf_alloc(STREAM_PRESZ + 4096);
	tx->sampv = mem_zalloc(AUDIO_SAMPSZ * 2, NULL);
	rx->sampv = mem_zalloc(AUDIO_SAMPSZ * 2, NULL);
	if (!tx->mb || !tx->sampv || !rx->sampv) {
		err = ENOMEM;
		goto out;
	}

	err = telev_alloc(&a->telev, TELEV_PTIME);
	if (err)
		goto out;

	err = add_telev_codec(a);
	if (err)
		goto out;

	auresamp_init(&tx->resamp);
	str_ncpy(tx->device, a->cfg.src_dev, sizeof(tx->device));
	tx->ptime  = ptime;
	tx->ts     = rand_u16();
	tx->marker = true;

	auresamp_init(&rx->resamp);
	str_ncpy(rx->device, a->cfg.play_dev, sizeof(rx->device));
	rx->pt     = -1;
	rx->ptime  = ptime;

	a->eventh  = eventh;
	a->errh    = errh;
	a->arg     = arg;

	if (a->cfg.txmode == AUDIO_MODE_TMR)
		tmr_init(&tx->u.tmr);

 out:
	if (err)
		mem_deref(a);
	else
		*ap = a;

	return err;
}


#ifdef HAVE_PTHREAD
static void *tx_thread(void *arg)
{
	struct audio *a = arg;
	struct autx *tx = &a->tx;
	unsigned i;

	/* Enable Real-time mode for this thread, if available */
	if (a->cfg.txmode == AUDIO_MODE_THREAD_REALTIME)
		(void)realtime_enable(true, 1);

	while (a->tx.u.thr.run) {

		for (i=0; i<16; i++) {

			if (aubuf_cur_size(tx->aubuf) < tx->psize)
				break;

			poll_aubuf_tx(a);
		}

		sys_msleep(5);
	}

	return NULL;
}
#endif


static void timeout_tx(void *arg)
{
	struct audio *a = arg;
	struct autx *tx = &a->tx;
	unsigned i;

	tmr_start(&a->tx.u.tmr, 5, timeout_tx, a);

	for (i=0; i<16; i++) {

		if (aubuf_cur_size(tx->aubuf) < tx->psize)
			break;

		poll_aubuf_tx(a);
	}
}


static void aufilt_param_set(struct aufilt_prm *prm,
			     const struct aucodec *ac, uint32_t ptime)
{
	if (!ac) {
		memset(prm, 0, sizeof(*prm));
		return;
	}

	prm->srate      = get_srate(ac);
	prm->ch         = get_ch(ac);
	prm->ptime      = ptime;
}


static int autx_print_pipeline(struct re_printf *pf, const struct autx *autx)
{
	struct le *le;
	int err;

	if (!autx)
		return 0;

	err = re_hprintf(pf, "audio tx pipeline:  %10s",
			 autx->ausrc ? autx->ausrc->as->name : "src");

	for (le = list_head(&autx->filtl); le; le = le->next) {
		struct aufilt_enc_st *st = le->data;

		if (st->af->ench)
			err |= re_hprintf(pf, " ---> %s", st->af->name);
	}

	err |= re_hprintf(pf, " ---> %s\n",
			  autx->ac ? autx->ac->name : "encoder");

	return err;
}


static int aurx_print_pipeline(struct re_printf *pf, const struct aurx *aurx)
{
	struct le *le;
	int err;

	if (!aurx)
		return 0;

	err = re_hprintf(pf, "audio rx pipeline:  %10s",
			 aurx->auplay ? aurx->auplay->ap->name : "play");

	for (le = list_head(&aurx->filtl); le; le = le->next) {
		struct aufilt_dec_st *st = le->data;

		if (st->af->dech)
			err |= re_hprintf(pf, " <--- %s", st->af->name);
	}

	err |= re_hprintf(pf, " <--- %s\n",
			  aurx->ac ? aurx->ac->name : "decoder");

	return err;
}


/**
 * Setup the audio-filter chain
 *
 * must be called before auplay/ausrc-alloc
 *
 * @param a Audio object
 *
 * @return 0 if success, otherwise errorcode
 */
static int aufilt_setup(struct audio *a)
{
	struct aufilt_prm encprm, decprm;
	struct autx *tx = &a->tx;
	struct aurx *rx = &a->rx;
	struct le *le;
	int err = 0;

	/* wait until we have both Encoder and Decoder */
	if (!tx->ac || !rx->ac)
		return 0;

	if (!list_isempty(&tx->filtl) || !list_isempty(&rx->filtl))
		return 0;

	aufilt_param_set(&encprm, tx->ac, tx->ptime);
	aufilt_param_set(&decprm, rx->ac, rx->ptime);

	/* Audio filters */
	for (le = list_head(aufilt_list()); le; le = le->next) {
		struct aufilt *af = le->data;
		struct aufilt_enc_st *encst = NULL;
		struct aufilt_dec_st *decst = NULL;
		void *ctx = NULL;

		if (af->encupdh) {
			err |= af->encupdh(&encst, &ctx, af, &encprm);
			if (err)
				break;

			encst->af = af;
			list_append(&tx->filtl, &encst->le, encst);
		}

		if (af->decupdh) {
			err |= af->decupdh(&decst, &ctx, af, &decprm);
			if (err)
				break;

			decst->af = af;
			list_append(&rx->filtl, &decst->le, decst);
		}

		if (err) {
			warning("audio: audio-filter '%s'"
				" update failed (%m)\n", af->name, err);
			break;
		}
	}

	return 0;
}


static int start_player(struct aurx *rx, struct audio *a)
{
	const struct aucodec *ac = rx->ac;
	uint32_t srate_dsp = get_srate(ac);
	uint32_t channels_dsp;
	bool resamp = false;
	int err;

	if (!ac)
		return 0;

	channels_dsp = get_ch(ac);

	if (a->cfg.srate_play && a->cfg.srate_play != srate_dsp) {
		resamp = true;
		srate_dsp = a->cfg.srate_play;
	}
	if (a->cfg.channels_play && a->cfg.channels_play != channels_dsp) {
		resamp = true;
		channels_dsp = a->cfg.channels_play;
	}

	/* Optional resampler, if configured */
	if (resamp && !rx->sampv_rs) {

		info("audio: enable auplay resampler:"
		     " %uHz/%uch --> %uHz/%uch\n",
		     get_srate(ac), get_ch(ac), srate_dsp, channels_dsp);

		rx->sampv_rs = mem_zalloc(AUDIO_SAMPSZ * 2, NULL);
		if (!rx->sampv_rs)
			return ENOMEM;

		err = auresamp_setup(&rx->resamp,
				     get_srate(ac), get_ch(ac),
				     srate_dsp, channels_dsp);
		if (err) {
			warning("audio: could not setup auplay resampler"
				" (%m)\n", err);
			return err;
		}
	}

	/* Start Audio Player */
	if (!rx->auplay && auplay_find(NULL)) {

		struct auplay_prm prm;

		prm.srate      = srate_dsp;
		prm.ch         = channels_dsp;
		prm.ptime      = rx->ptime;

		if (!rx->aubuf) {
			size_t psize;

			psize = 2 * calc_nsamp(prm.srate, prm.ch, prm.ptime);

			err = aubuf_alloc(&rx->aubuf, psize * 1, psize * 8);
			if (err)
				return err;
		}

		err = auplay_alloc(&rx->auplay, a->cfg.play_mod,
				   &prm, rx->device,
				   auplay_write_handler, rx);
		if (err) {
			warning("audio: start_player failed (%s.%s): %m\n",
				a->cfg.play_mod, rx->device, err);
			return err;
		}

		rx->auplay_prm = prm;
	}

	return 0;
}


static int start_source(struct autx *tx, struct audio *a)
{
	const struct aucodec *ac = tx->ac;
	uint32_t srate_dsp = get_srate(ac);
	uint32_t channels_dsp;
	bool resamp = false;
	int err;

	if (!ac)
		return 0;

	channels_dsp = get_ch(ac);

	if (a->cfg.srate_src && a->cfg.srate_src != srate_dsp) {
		resamp = true;
		srate_dsp = a->cfg.srate_src;
	}
	if (a->cfg.channels_src && a->cfg.channels_src != channels_dsp) {
		resamp = true;
		channels_dsp = a->cfg.channels_src;
	}

	/* Optional resampler, if configured */
	if (resamp && !tx->sampv_rs) {

		info("audio: enable ausrc resampler:"
		     " %uHz/%uch <-- %uHz/%uch\n",
		     get_srate(ac), get_ch(ac), srate_dsp, channels_dsp);

		tx->sampv_rs = mem_zalloc(AUDIO_SAMPSZ * 2, NULL);
		if (!tx->sampv_rs)
			return ENOMEM;

		err = auresamp_setup(&tx->resamp,
				     srate_dsp, channels_dsp,
				     get_srate(ac), get_ch(ac));
		if (err) {
			warning("audio: could not setup ausrc resampler"
				" (%m)\n", err);
			return err;
		}
	}

	/* Start Audio Source */
	if (!tx->ausrc && ausrc_find(NULL)) {

		struct ausrc_prm prm;

		prm.srate      = srate_dsp;
		prm.ch         = channels_dsp;
		prm.ptime      = tx->ptime;

		tx->psize = 2 * calc_nsamp(prm.srate, prm.ch, prm.ptime);

		if (!tx->aubuf) {
			err = aubuf_alloc(&tx->aubuf, tx->psize * 2,
					  tx->psize * 30);
			if (err)
				return err;
		}

		err = ausrc_alloc(&tx->ausrc, NULL, a->cfg.src_mod,
				  &prm, tx->device,
				  ausrc_read_handler, ausrc_error_handler, a);
		if (err) {
			warning("audio: start_source failed (%s.%s): %m\n",
				a->cfg.src_mod, tx->device, err);
			return err;
		}

		switch (a->cfg.txmode) {
#ifdef HAVE_PTHREAD
		case AUDIO_MODE_THREAD:
		case AUDIO_MODE_THREAD_REALTIME:
			if (!tx->u.thr.run) {
				tx->u.thr.run = true;
				err = pthread_create(&tx->u.thr.tid, NULL,
						     tx_thread, a);
				if (err) {
					tx->u.thr.tid = false;
					return err;
				}
			}
			break;
#endif

		case AUDIO_MODE_TMR:
			tmr_start(&tx->u.tmr, 1, timeout_tx, a);
			break;

		default:
			break;
		}

		tx->ausrc_prm = prm;
	}

	return 0;
}


/**
 * Start the audio playback and recording
 *
 * @param a Audio object
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_start(struct audio *a)
{
	int err;

	if (!a)
		return EINVAL;

	/* Audio filter */
	if (!list_isempty(aufilt_list())) {
		err = aufilt_setup(a);
		if (err)
			return err;
	}

	/* configurable order of play/src start */
	if (a->cfg.src_first) {
		err  = start_source(&a->tx, a);
		err |= start_player(&a->rx, a);
	}
	else {
		err  = start_player(&a->rx, a);
		err |= start_source(&a->tx, a);
	}
	if (err)
		return err;

	if (a->tx.ac && a->rx.ac) {

		if (!a->started) {
			info("%H%H",
			     autx_print_pipeline, &a->tx,
			     aurx_print_pipeline, &a->rx);
		}

		a->started = true;
	}

	return err;
}


/**
 * Stop the audio playback and recording
 *
 * @param a Audio object
 */
void audio_stop(struct audio *a)
{
	if (!a)
		return;

	stop_tx(&a->tx, a);
	stop_rx(&a->rx);
}


int audio_encoder_set(struct audio *a, const struct aucodec *ac,
		      int pt_tx, const char *params)
{
	struct autx *tx;
	int err = 0;
	bool reset;

	if (!a || !ac)
		return EINVAL;

	tx = &a->tx;

	reset = !aucodec_equal(ac, tx->ac);

	if (ac != tx->ac) {
		info("audio: Set audio encoder: %s %uHz %dch\n",
		     ac->name, get_srate(ac), get_ch(ac));

		/* Audio source must be stopped first */
		if (reset) {
			tx->ausrc = mem_deref(tx->ausrc);
		}

		tx->enc = mem_deref(tx->enc);
		tx->ac = ac;
	}

	if (ac->encupdh) {
		struct auenc_param prm;

		prm.ptime = tx->ptime;

		err = ac->encupdh(&tx->enc, ac, &prm, params);
		if (err) {
			warning("audio: alloc encoder: %m\n", err);
			return err;
		}
	}

	stream_set_srate(a->strm, ac->crate, ac->crate);
	stream_update_encoder(a->strm, pt_tx);

	if (!tx->ausrc) {
		err |= audio_start(a);
	}

	return err;
}


int audio_decoder_set(struct audio *a, const struct aucodec *ac,
		      int pt_rx, const char *params)
{
	struct aurx *rx;
	bool reset = false;
	int err = 0;

	if (!a || !ac)
		return EINVAL;

	rx = &a->rx;

	reset = !aucodec_equal(ac, rx->ac);

	if (ac != rx->ac) {

		info("audio: Set audio decoder: %s %uHz %dch\n",
		     ac->name, get_srate(ac), get_ch(ac));

		rx->pt = pt_rx;
		rx->ac = ac;
		rx->dec = mem_deref(rx->dec);
	}

	if (ac->decupdh) {
		err = ac->decupdh(&rx->dec, ac, params);
		if (err) {
			warning("audio: alloc decoder: %m\n", err);
			return err;
		}
	}

	stream_set_srate(a->strm, ac->crate, ac->crate);

	if (reset) {

		rx->auplay = mem_deref(rx->auplay);

		/* Reset audio filter chain */
		list_flush(&rx->filtl);

		err |= audio_start(a);
	}

	return err;
}


/**
 * Use the next audio encoder in the local list of negotiated codecs
 *
 * @param audio  Audio object
 */
void audio_encoder_cycle(struct audio *audio)
{
	const struct sdp_format *rc = NULL;

	if (!audio)
		return;

	rc = sdp_media_format_cycle(stream_sdpmedia(audio_strm(audio)));
	if (!rc) {
		info("audio: encoder cycle: no remote codec found\n");
		return;
	}

	(void)audio_encoder_set(audio, rc->data, rc->pt, rc->params);
}


struct stream *audio_strm(const struct audio *a)
{
	return a ? a->strm : NULL;
}


int audio_send_digit(struct audio *a, char key)
{
	int err = 0;

	if (!a)
		return EINVAL;

	if (key != KEYCODE_REL) {
		int event = telev_digit2code(key);
		info("audio: send DTMF digit: '%c'\n", key);

		if (event == -1) {
			warning("audio: invalid DTMF digit (0x%02x)\n", key);
			return EINVAL;
		}

		err = telev_send(a->telev, event, false);
	}
	else if (a->tx.cur_key && a->tx.cur_key != KEYCODE_REL) {
		/* Key release */
		info("audio: send DTMF digit end: '%c'\n", a->tx.cur_key);
		err = telev_send(a->telev,
				 telev_digit2code(a->tx.cur_key), true);
	}

	a->tx.cur_key = key;

	return err;
}


/**
 * Mute the audio stream source (i.e. Microphone)
 *
 * @param a      Audio stream
 * @param muted  True to mute, false to un-mute
 */
void audio_mute(struct audio *a, bool muted)
{
	if (!a)
		return;

	a->tx.muted = muted;
}


/**
 * Get the mute state of an audio source
 *
 * @param a      Audio stream
 *
 * @return True if muted, otherwise false
 */
bool audio_ismuted(const struct audio *a)
{
	if (!a)
		return false;

	return a->tx.muted;
}


void audio_sdp_attr_decode(struct audio *a)
{
	const char *attr;

	if (!a)
		return;

	/* This is probably only meaningful for audio data, but
	   may be used with other media types if it makes sense. */
	attr = sdp_media_rattr(stream_sdpmedia(a->strm), "ptime");
	if (attr) {
		struct autx *tx = &a->tx;
		uint32_t ptime_tx = atoi(attr);

		if (ptime_tx && ptime_tx != a->tx.ptime) {

			info("audio: peer changed ptime_tx %ums -> %ums\n",
			     a->tx.ptime, ptime_tx);

			tx->ptime = ptime_tx;

			if (tx->ac) {
				tx->psize = 2 * get_framesize(tx->ac,
							      ptime_tx);
			}
		}
	}
}


static int aucodec_print(struct re_printf *pf, const struct aucodec *ac)
{
	if (!ac)
		return 0;

	return re_hprintf(pf, "%s %uHz/%dch",
			  ac->name, get_srate(ac), get_ch(ac));
}


int audio_debug(struct re_printf *pf, const struct audio *a)
{
	const struct autx *tx;
	const struct aurx *rx;
	int err;

	if (!a)
		return 0;

	tx = &a->tx;
	rx = &a->rx;

	err  = re_hprintf(pf, "\n--- Audio stream ---\n");

	err |= re_hprintf(pf, " tx:   %H %H ptime=%ums\n",
			  aucodec_print, tx->ac,
			  aubuf_debug, tx->aubuf,
			  tx->ptime);

	err |= re_hprintf(pf, " rx:   %H %H ptime=%ums pt=%d\n",
			  aucodec_print, rx->ac,
			  aubuf_debug, rx->aubuf,
			  rx->ptime, rx->pt);

	err |= re_hprintf(pf,
			  " %H"
			  " %H",
			  autx_print_pipeline, tx,
			  aurx_print_pipeline, rx);

	err |= stream_debug(pf, a->strm);

	return err;
}


void audio_set_devicename(struct audio *a, const char *src, const char *play)
{
	if (!a)
		return;

	str_ncpy(a->tx.device, src, sizeof(a->tx.device));
	str_ncpy(a->rx.device, play, sizeof(a->rx.device));
}


int audio_set_source(struct audio *au, const char *mod, const char *device)
{
	struct autx *tx;
	int err;

	if (!au)
		return EINVAL;

	tx = &au->tx;

	/* stop the audio device first */
	tx->ausrc = mem_deref(tx->ausrc);

	err = ausrc_alloc(&tx->ausrc, NULL, mod, &tx->ausrc_prm, device,
			  ausrc_read_handler, ausrc_error_handler, au);
	if (err) {
		warning("audio: set_source failed (%s.%s): %m\n",
			mod, device, err);
		return err;
	}

	return 0;
}


int audio_set_player(struct audio *au, const char *mod, const char *device)
{
	struct aurx *rx;
	int err;

	if (!au)
		return EINVAL;

	rx = &au->rx;

	/* stop the audio device first */
	rx->auplay = mem_deref(rx->auplay);

	err = auplay_alloc(&rx->auplay, mod, &rx->auplay_prm, device,
			   auplay_write_handler, rx);
	if (err) {
		warning("audio: set_player failed (%s.%s): %m\n",
			mod, device, err);
		return err;
	}

	return 0;
}


/*
 * Reference:
 *
 * https://www.avm.de/de/Extern/files/x-rtp/xrtpv32.pdf
 */
int audio_print_rtpstat(struct re_printf *pf, const struct audio *a)
{
	const struct stream *s;
	const struct rtcp_stats *rtcp;
	int srate_tx = 8000;
	int srate_rx = 8000;
	int err;

	if (!a)
		return 1;

	s = a->strm;
	rtcp = &s->rtcp_stats;

	if (!rtcp->tx.sent)
		return 1;

	if (a->tx.ac)
		srate_tx = get_srate(a->tx.ac);
	if (a->rx.ac)
		srate_rx = get_srate(a->rx.ac);

	err = re_hprintf(pf,
			 "EX=BareSip;"   /* Reporter Identifier	             */
			 "CS=%d;"        /* Call Setup in milliseconds       */
			 "CD=%d;"        /* Call Duration in seconds	     */
			 "PR=%u;PS=%u;"  /* Packets RX, TX                   */
			 "PL=%d,%d;"     /* Packets Lost RX, TX              */
			 "PD=%d,%d;"     /* Packets Discarded, RX, TX        */
			 "JI=%.1f,%.1f;" /* Jitter RX, TX in timestamp units */
			 "IP=%J,%J"      /* Local, Remote IPs                */
			 ,
			 call_setup_duration(s->call) * 1000,
			 call_duration(s->call),

			 s->metric_rx.n_packets,
			 s->metric_tx.n_packets,

			 rtcp->rx.lost, rtcp->tx.lost,

			 s->metric_rx.n_err, s->metric_tx.n_err,

			 /* timestamp units (ie: 8 ts units = 1 ms @ 8KHZ) */
			 1.0 * rtcp->rx.jit/1000 * (srate_rx/1000),
			 1.0 * rtcp->tx.jit/1000 * (srate_tx/1000),

			 sdp_media_laddr(s->sdp),
			 sdp_media_raddr(s->sdp)
			 );

	if (a->tx.ac) {
		err |= re_hprintf(pf, ";EN=%s/%d", a->tx.ac->name, srate_tx );
	}
	if (a->rx.ac) {
		err |= re_hprintf(pf, ";DE=%s/%d", a->rx.ac->name, srate_rx );
	}

	return err;
}
