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
	MAX_SRATE       = 48000,  /* Maximum sample rate in [Hz] */
	MAX_CHANNELS    =     2,  /* Maximum number of channels  */
	MAX_PTIME       =    60,  /* Maximum packet time in [ms] */

	AUDIO_SAMPSZ    = MAX_SRATE * MAX_CHANNELS * MAX_PTIME / 1000,
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
	struct ausrc_prm ausrc_prm;   /**< Audio Source parameters         */
	const struct aucodec *ac;     /**< Current audio encoder           */
	struct auenc_state *enc;      /**< Audio encoder state (optional)  */
	struct aubuf *aubuf;          /**< Packetize outgoing stream       */
	size_t aubuf_maxsz;           /**< Maximum aubuf size in [bytes]   */
	volatile bool aubuf_started;  /**< Aubuf was started flag          */
	struct auresamp resamp;       /**< Optional resampler for DSP      */
	struct list filtl;            /**< Audio filters in encoding order */
	struct mbuf *mb;              /**< Buffer for outgoing RTP packets */
	struct media_ctx **ctx;       /**< Shared A/V source media context */
	char *module;                 /**< Audio source module name        */
	char *device;                 /**< Audio source device name        */
	void *sampv;                  /**< Sample buffer                   */
	int16_t *sampv_rs;            /**< Sample buffer for resampler     */
	uint32_t ptime;               /**< Packet time for sending         */
	uint64_t ts_ext;              /**< Ext. Timestamp for outgoing RTP */
	uint32_t ts_base;             /**< First timestamp sent            */
	uint32_t ts_tel;              /**< Timestamp for Telephony Events  */
	size_t psize;                 /**< Packet size for sending         */
	bool marker;                  /**< Marker bit for outgoing RTP     */
	bool muted;                   /**< Audio source is muted           */
	int cur_key;                  /**< Currently transmitted event     */
	enum aufmt src_fmt;           /**< Sample format for audio source  */
	enum aufmt enc_fmt;           /**< Sample format for encoder       */
	bool need_conv;               /**< Sample format conversion needed */

	struct {
		uint64_t aubuf_overrun;
		uint64_t aubuf_underrun;
	} stats;

#ifdef HAVE_PTHREAD
	union {
		struct {
			pthread_t tid;/**< Audio transmit thread           */
			bool run;     /**< Audio transmit thread running   */
		} thr;
	} u;
#endif
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
	struct auplay_prm auplay_prm; /**< Audio Player parameters         */
	const struct aucodec *ac;     /**< Current audio decoder           */
	struct audec_state *dec;      /**< Audio decoder state (optional)  */
	struct aubuf *aubuf;          /**< Incoming audio buffer           */
	size_t aubuf_maxsz;           /**< Maximum aubuf size in [bytes]   */
	volatile bool aubuf_started;  /**< Aubuf was started flag          */
	struct auresamp resamp;       /**< Optional resampler for DSP      */
	struct list filtl;            /**< Audio filters in decoding order */
	char *module;                 /**< Audio player module name        */
	char *device;                 /**< Audio player device name        */
	void *sampv;                  /**< Sample buffer                   */
	int16_t *sampv_rs;            /**< Sample buffer for resampler     */
	uint32_t ptime;               /**< Packet time for receiving       */
	int pt;                       /**< Payload type for incoming RTP   */
	double level_last;            /**< Last audio level value [dBov]   */
	bool level_set;               /**< True if level_last is set       */
	enum aufmt play_fmt;          /**< Sample format for audio playback*/
	enum aufmt dec_fmt;           /**< Sample format for decoder       */
	bool need_conv;               /**< Sample format conversion needed */
	struct timestamp_recv ts_recv;/**< Receive timestamp state         */
	size_t last_sampc;

	struct {
		uint64_t aubuf_overrun;
		uint64_t aubuf_underrun;
		uint64_t n_discard;
	} stats;
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
	bool level_enabled;           /**< Audio level RTP ext. enabled    */
	bool hold;                    /**< Local hold flag                 */
	unsigned extmap_aulevel;      /**< ID Range 1-14 inclusive         */
	audio_event_h *eventh;        /**< Event handler                   */
	audio_level_h *levelh;        /**< Audio level handler             */
	audio_err_h *errh;            /**< Audio error handler             */
	void *arg;                    /**< Handler argument                */
};


/* RFC 6464 */
static const char *uri_aulevel = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";


/**
 * Get the current audio receive buffer length in milliseconds
 *
 * @param au Audio object
 *
 * @return Audio buffer length in [ms]
 */
uint64_t audio_jb_current_value(const struct audio *au)
{
	const struct aurx *rx;

	if (!au)
		return 0;

	rx = &au->rx;

	if (rx->aubuf) {
		uint64_t b_p_ms;  /* bytes per ms */

		b_p_ms = aufmt_sample_size(rx->play_fmt) *
			rx->auplay_prm.srate * rx->auplay_prm.ch / 1000;

		if (b_p_ms) {
			uint64_t val;

			val = aubuf_cur_size(rx->aubuf) / b_p_ms;

			return val;
		}
	}

	return 0;
}


static double autx_calc_seconds(const struct autx *autx)
{
	uint64_t dur;

	if (!autx->ac)
		return .0;

	dur = autx->ts_ext - autx->ts_base;

	return timestamp_calc_seconds(dur, autx->ac->crate);
}


static double aurx_calc_seconds(const struct aurx *aurx)
{
	uint64_t dur;

	if (!aurx->ac)
		return .0;

	dur = timestamp_duration(&aurx->ts_recv);

	return timestamp_calc_seconds(dur, aurx->ac->crate);
}


static void stop_tx(struct autx *tx, struct audio *a)
{
	if (!tx || !a)
		return;

	switch (a->cfg.txmode) {

#ifdef HAVE_PTHREAD
	case AUDIO_MODE_THREAD:
		if (tx->u.thr.run) {
			tx->u.thr.run = false;
			pthread_join(tx->u.thr.tid, NULL);
		}
		break;
#endif
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

	debug("audio: destroyed (started=%d)\n", a->started);

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
	mem_deref(a->tx.module);
	mem_deref(a->tx.device);
	mem_deref(a->rx.module);
	mem_deref(a->rx.device);

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


static inline double calc_ptime(size_t nsamp, uint32_t srate, uint8_t channels)
{
	double ptime;

	ptime = 1000.0 * (double)nsamp / (double)(srate * channels);

	return ptime;
}


static bool aucodec_equal(const struct aucodec *a, const struct aucodec *b)
{
	if (!a || !b)
		return false;

	return a->srate == b->srate && a->ch == b->ch;
}


static int add_audio_codec(struct sdp_media *m, struct aucodec *ac)
{
	if (ac->crate < 8000) {
		warning("audio: illegal clock rate %u\n", ac->crate);
		return EINVAL;
	}

	if (ac->ch == 0 || ac->pch == 0) {
		warning("audio: illegal channels for audio codec '%s'\n",
			ac->name);
		return EINVAL;
	}

	return sdp_format_add(NULL, m, false, ac->pt, ac->name, ac->crate,
			      ac->pch, ac->fmtp_ench, ac->fmtp_cmph, ac, false,
			      "%s", ac->fmtp);
}


static int append_rtpext(struct audio *au, struct mbuf *mb,
			 enum aufmt fmt, const void *sampv, size_t sampc)
{
	uint8_t data[1];
	double level;
	int err;

	/* audio level must be calculated from the audio samples that
	 * are actually sent on the network. */
	level = aulevel_calc_dbov(fmt, sampv, sampc);

	data[0] = (int)-level & 0x7f;

	err = rtpext_encode(mb, au->extmap_aulevel, 1, data);
	if (err) {
		warning("audio: rtpext_encode failed (%m)\n", err);
		return err;
	}

	return err;
}


/*
 * Encode audio and send via stream
 *
 * @note This function has REAL-TIME properties
 */
static void encode_rtp_send(struct audio *a, struct autx *tx,
			    int16_t *sampv, size_t sampc)
{
	size_t frame_size;  /* number of samples per channel */
	size_t sampc_rtp;
	size_t len;
	size_t ext_len = 0;
	uint32_t ts_delta = 0;
	bool marker = tx->marker;
	int err;

	if (!tx->ac || !tx->ac->ench)
		return;

	tx->mb->pos = tx->mb->end = STREAM_PRESZ;

	if (a->level_enabled) {

		/* skip the extension header */
		tx->mb->pos += RTPEXT_HDR_SIZE;

		err = append_rtpext(a, tx->mb, tx->enc_fmt, sampv, sampc);
		if (err)
			return;

		ext_len = tx->mb->pos - STREAM_PRESZ;

		/* write the Extension header at the beginning */
		tx->mb->pos = STREAM_PRESZ;

		err = rtpext_hdr_encode(tx->mb, ext_len - RTPEXT_HDR_SIZE);
		if (err)
			return;

		tx->mb->pos = STREAM_PRESZ + ext_len;
		tx->mb->end = STREAM_PRESZ + ext_len;
	}

	len = mbuf_get_space(tx->mb);

	err = tx->ac->ench(tx->enc, &marker, mbuf_buf(tx->mb), &len,
			   tx->enc_fmt, sampv, sampc);

	if ((err & 0xffff0000) == 0x00010000) {

		/* MPA needs some special treatment here */

		ts_delta = err & 0xffff;
		sampc = 0;
	}
	else if (err) {
		warning("audio: %s encode error: %d samples (%m)\n",
			tx->ac->name, sampc, err);
		goto out;
	}

	tx->mb->pos = STREAM_PRESZ;
	tx->mb->end = STREAM_PRESZ + ext_len + len;

	if (mbuf_get_left(tx->mb)) {

		uint32_t rtp_ts = tx->ts_ext & 0xffffffff;

		if (len) {
			err = stream_send(a->strm, ext_len!=0, marker, -1,
					  rtp_ts, tx->mb);
			if (err)
				goto out;
		}

		if (ts_delta) {
			tx->ts_ext += ts_delta;
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
	frame_size = sampc_rtp / tx->ac->ch;

	tx->ts_ext += (uint32_t)frame_size;

 out:
	tx->marker = false;
}


/*
 * @note This function has REAL-TIME properties
 */
static void poll_aubuf_tx(struct audio *a)
{
	struct autx *tx = &a->tx;
	struct auframe af;
	int16_t *sampv = tx->sampv;
	size_t sampc;
	size_t sz;
	size_t num_bytes;
	struct le *le;
	int err = 0;

	sz = aufmt_sample_size(tx->src_fmt);
	if (!sz)
		return;

	num_bytes = tx->psize;
	sampc = tx->psize / sz;

	/* timed read from audio-buffer */

	if (tx->src_fmt == tx->enc_fmt) {

		aubuf_read(tx->aubuf, (uint8_t *)tx->sampv, num_bytes);
	}
	else if (tx->enc_fmt == AUFMT_S16LE) {

		/* Convert from ausrc format to 16-bit format */

		void *tmp_sampv;

		if (!tx->need_conv) {
			info("audio: NOTE: source sample conversion"
			     " needed: %s  -->  %s\n",
			     aufmt_name(tx->src_fmt), aufmt_name(AUFMT_S16LE));
			tx->need_conv = true;
		}

		tmp_sampv = mem_zalloc(num_bytes, NULL);
		if (!tmp_sampv)
			return;

		aubuf_read(tx->aubuf, tmp_sampv, num_bytes);

		auconv_to_s16(sampv, tx->src_fmt, tmp_sampv, sampc);

		mem_deref(tmp_sampv);
	}
	else {
		warning("audio: tx: invalid sample formats (%s -> %s)\n",
			aufmt_name(tx->src_fmt),
			aufmt_name(tx->enc_fmt));
	}

	/* optional resampler */
	if (tx->resamp.resample) {
		size_t sampc_rs = AUDIO_SAMPSZ;

		if (tx->enc_fmt != AUFMT_S16LE) {
			warning("audio: skipping resampler due to"
				" incompatible format (%s)\n",
				aufmt_name(tx->enc_fmt));
			return;
		}

		err = auresamp(&tx->resamp,
			       tx->sampv_rs, &sampc_rs,
			       tx->sampv, sampc);
		if (err)
			return;

		sampv = tx->sampv_rs;
		sampc = sampc_rs;
	}

	auframe_init(&af, tx->enc_fmt, sampv, sampc);

	/* Process exactly one audio-frame in list order */
	for (le = tx->filtl.head; le; le = le->next) {
		struct aufilt_enc_st *st = le->data;

		if (st->af && st->af->ench)
			err |= st->af->ench(st, &af);
	}
	if (err) {
		warning("audio: aufilter encode: %m\n", err);
	}

	/* Encode and send */
	encode_rtp_send(a, tx, af.sampv, af.sampc);
}


static void check_telev(struct audio *a, struct autx *tx)
{
	const struct sdp_format *fmt;
	struct mbuf *mb;
	bool marker = false;
	int err;

	mb = mbuf_alloc(STREAM_PRESZ + 64);
	if (!mb)
		return;

	mb->pos = mb->end = STREAM_PRESZ;

	err = telev_poll(a->telev, &marker, mb);
	if (err)
		goto out;

	if (marker)
		tx->ts_tel = (uint32_t)tx->ts_ext;

	fmt = sdp_media_rformat(stream_sdpmedia(audio_strm(a)), telev_rtpfmt);
	if (!fmt)
		goto out;

	mb->pos = STREAM_PRESZ;
	err = stream_send(a->strm, false, marker, fmt->pt, tx->ts_tel, mb);
	if (err) {
		warning("audio: telev: stream_send %m\n", err);
	}

 out:
	mem_deref(mb);
}


/*
 * Write samples to Audio Player.
 *
 * @note This function has REAL-TIME properties
 *
 * @note The application is responsible for filling in silence in
 *       the case of underrun
 *
 * @note This function may be called from any thread
 *
 * @note The sample format is set in rx->play_fmt
 */
static void auplay_write_handler(void *sampv, size_t sampc, void *arg)
{
	struct aurx *rx = arg;
	size_t num_bytes = sampc * aufmt_sample_size(rx->play_fmt);

	if (rx->aubuf_started && aubuf_cur_size(rx->aubuf) < num_bytes) {

		++rx->stats.aubuf_underrun;

#if 0
		debug("audio: rx aubuf underrun (total %llu)\n",
			rx->stats.aubuf_underrun);
#endif
	}

	aubuf_read(rx->aubuf, sampv, num_bytes);
}


/*
 * Read samples from Audio Source
 *
 * @note This function has REAL-TIME properties
 *
 * @note This function may be called from any thread
 */
static void ausrc_read_handler(struct auframe *af, void *arg)
{
	struct audio *a = arg;
	struct autx *tx = &a->tx;
	size_t num_bytes = auframe_size(af);

	if ((int)tx->src_fmt != af->fmt) {
		warning("audio: ausrc format mismatch:"
			" expected=%d(%s), actual=%d(%s)\n",
			tx->src_fmt, aufmt_name(tx->src_fmt),
			af->fmt, aufmt_name(af->fmt));
		return;
	}

	if (tx->muted)
		auframe_mute(af);

	if (aubuf_cur_size(tx->aubuf) >= tx->aubuf_maxsz) {

		++tx->stats.aubuf_overrun;

		debug("audio: tx aubuf overrun (total %llu)\n",
		      tx->stats.aubuf_overrun);
	}

	(void)aubuf_write(tx->aubuf, af->sampv, num_bytes);

	tx->aubuf_started = true;

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


static int update_payload_type(struct audio *a, uint8_t pt_old, uint8_t pt_new)
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


static int aurx_stream_decode(struct aurx *rx, bool marker,
			      struct mbuf *mb, unsigned lostc)
{
	struct auframe af;
	size_t sampc = AUDIO_SAMPSZ;
	void *sampv;
	struct le *le;
	int err = 0;

	/* No decoder set */
	if (!rx->ac)
		return 0;

	if (lostc && rx->ac->plch) {

		err = rx->ac->plch(rx->dec,
				   rx->dec_fmt, rx->sampv, &sampc,
				   mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio: %s codec decode %u bytes: %m\n",
				rx->ac->name, mbuf_get_left(mb), err);
			goto out;
		}
	}
	else if (mbuf_get_left(mb)) {

		err = rx->ac->dech(rx->dec,
				   rx->dec_fmt, rx->sampv, &sampc,
				   marker, mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio: %s codec decode %u bytes: %m\n",
				rx->ac->name, mbuf_get_left(mb), err);
			goto out;
		}

		rx->last_sampc = sampc;
	}
	else {
		/* no PLC in the codec, might be done in filters below */
		sampc = 0;
	}

	auframe_init(&af, rx->dec_fmt, rx->sampv, sampc);

	/* Process exactly one audio-frame in reverse list order */
	for (le = rx->filtl.tail; le; le = le->prev) {
		struct aufilt_dec_st *st = le->data;

		if (st->af && st->af->dech)
			err |= st->af->dech(st, &af);
	}

	if (!rx->aubuf)
		goto out;

	sampv = af.sampv;
	sampc = af.sampc;

	/* optional resampler */
	if (rx->resamp.resample) {
		size_t sampc_rs = AUDIO_SAMPSZ;

		if (rx->dec_fmt != AUFMT_S16LE) {
			warning("audio: skipping resampler due to"
				" incompatible format (%s)\n",
				aufmt_name(rx->dec_fmt));
			return ENOTSUP;
		}

		err = auresamp(&rx->resamp,
			       rx->sampv_rs, &sampc_rs,
			       rx->sampv, sampc);
		if (err)
			return err;

		sampv = rx->sampv_rs;
		sampc = sampc_rs;
	}

	if (aubuf_cur_size(rx->aubuf) >= rx->aubuf_maxsz) {

		++rx->stats.aubuf_overrun;

#if 0
		debug("audio: rx aubuf overrun (total %llu)\n",
		      rx->stats.aubuf_overrun);
#endif
	}

	if (rx->play_fmt == rx->dec_fmt) {

		size_t num_bytes = sampc * aufmt_sample_size(rx->play_fmt);

		err = aubuf_write(rx->aubuf, sampv, num_bytes);
		if (err)
			goto out;
	}
	else if (rx->dec_fmt == AUFMT_S16LE) {

		/* Convert from 16-bit to auplay format */
		void *tmp_sampv;
		size_t num_bytes = sampc * aufmt_sample_size(rx->play_fmt);

		if (!rx->need_conv) {
			info("audio: NOTE: playback sample conversion"
			     " needed: %s  -->  %s\n",
			     aufmt_name(AUFMT_S16LE),
			     aufmt_name(rx->play_fmt));
			rx->need_conv = true;
		}

		tmp_sampv = mem_zalloc(num_bytes, NULL);
		if (!tmp_sampv)
			return ENOMEM;

		auconv_from_s16(rx->play_fmt, tmp_sampv, sampv, sampc);

		err = aubuf_write(rx->aubuf, tmp_sampv, num_bytes);

		mem_deref(tmp_sampv);

		if (err)
			goto out;
	}
	else {
		warning("audio: decode: invalid sample formats (%s -> %s)\n",
			aufmt_name(rx->dec_fmt),
			aufmt_name(rx->play_fmt));
	}

	rx->aubuf_started = true;

 out:
	return err;
}


/* Handle incoming stream data from the network */
static void stream_recv_handler(const struct rtp_header *hdr,
				struct rtpext *extv, size_t extc,
				struct mbuf *mb, unsigned lostc, void *arg)
{
	struct audio *a = arg;
	struct aurx *rx = &a->rx;
	bool discard = false;
	size_t i;
	int wrap;
	int err;

	MAGIC_CHECK(a);

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
	if (hdr->pt != rx->pt) {

		err = update_payload_type(a, rx->pt, hdr->pt);
		if (err)
			return;
	}

	/* RFC 5285 -- A General Mechanism for RTP Header Extensions */
	for (i=0; i<extc; i++) {

		if (extv[i].id == a->extmap_aulevel) {

			a->rx.level_last = -(double)(extv[i].data[0] & 0x7f);
			a->rx.level_set = true;
		}
		else {
			debug("audio: rtp header ext ignored (id=%u)\n",
			     extv[i].id);
		}
	}

	/* Save timestamp for incoming RTP packets */

	if (rx->ts_recv.is_set) {

		uint64_t ext_last, ext_now;

		ext_last = timestamp_calc_extended(rx->ts_recv.num_wraps,
						   rx->ts_recv.last);

		ext_now = timestamp_calc_extended(rx->ts_recv.num_wraps,
						  hdr->ts);

		if (ext_now <= ext_last) {
			uint64_t delta;

			delta = ext_last - ext_now;

			warning("audio: [time=%.3f]"
				" discard old frame (%.3f seconds old)\n",
				aurx_calc_seconds(rx),
				timestamp_calc_seconds(delta, rx->ac->crate));

			discard = true;
		}
	}
	else {
		timestamp_set(&rx->ts_recv, hdr->ts);
	}

	wrap = timestamp_wrap(hdr->ts, rx->ts_recv.last);

	switch (wrap) {

	case -1:
		warning("audio: rtp timestamp wraps backwards"
			" (delta = %d) -- discard\n",
			(int32_t)(rx->ts_recv.last - hdr->ts));
		discard = true;
		break;

	case 0:
		break;

	case 1:
		++rx->ts_recv.num_wraps;
		break;

	default:
		break;
	}

	rx->ts_recv.last = hdr->ts;

#if 0
	re_printf("[time=%.3f]    wrap=%d  discard=%d\n",
		  aurx_calc_seconds(rx), wrap, discard);
#endif

	if (discard) {
		++rx->stats.n_discard;
		return;
	}

 out:
	if (lostc)
		aurx_stream_decode(&a->rx, hdr->m, mb, lostc);

	(void)aurx_stream_decode(&a->rx, hdr->m, mb, 0);
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


/**
 * Allocate an audio stream
 *
 * @param ap         Pointer to allocated audio stream object
 * @param streaml    List of streams
 * @param stream_prm Stream parameters
 * @param cfg        Global configuration
 * @param acc        User-Agent account
 * @param sdp_sess   SDP Session
 * @param label      SDP label
 * @param mnat       Media NAT (optional)
 * @param mnat_sess  Media NAT session (optional)
 * @param menc       Media Encryption (optional)
 * @param menc_sess  Media Encryption session (optional)
 * @param ptime      Packet time in [ms]
 * @param aucodecl   List of audio codecs
 * @param offerer    True if SDP offerer, false if SDP answerer
 * @param eventh     Event handler
 * @param levelh     Audio level handler
 * @param errh       Error handler
 * @param arg        Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_alloc(struct audio **ap, struct list *streaml,
		const struct stream_param *stream_prm,
		const struct config *cfg,
		struct account *acc, struct sdp_session *sdp_sess, int label,
		const struct mnat *mnat, struct mnat_sess *mnat_sess,
		const struct menc *menc, struct menc_sess *menc_sess,
		uint32_t ptime, const struct list *aucodecl, bool offerer,
		audio_event_h *eventh, audio_level_h *levelh,
		audio_err_h *errh, void *arg)
{
	struct audio *a;
	struct autx *tx;
	struct aurx *rx;
	struct le *le;
	uint32_t minptime = ptime;
	int err;

	if (!ap || !cfg)
		return EINVAL;

	if (ptime < 1 || ptime > MAX_PTIME) {
		warning("audio: ptime %ums out of range (%ums - %ums)\n",
			ptime, 1, MAX_PTIME);
		return ENOTSUP;
	}

	a = mem_zalloc(sizeof(*a), audio_destructor);
	if (!a)
		return ENOMEM;

	MAGIC_INIT(a);

	a->cfg = cfg->audio;
	tx = &a->tx;
	rx = &a->rx;

	tx->src_fmt = cfg->audio.src_fmt;
	rx->play_fmt = cfg->audio.play_fmt;

	tx->enc_fmt = cfg->audio.enc_fmt;
	rx->dec_fmt = cfg->audio.dec_fmt;

	err = stream_alloc(&a->strm, streaml,
			   stream_prm, &cfg->avt, sdp_sess,
			   MEDIA_AUDIO, label,
			   mnat, mnat_sess, menc, menc_sess, offerer,
			   stream_recv_handler, NULL, a);
	if (err)
		goto out;

	if (cfg->avt.rtp_bw.max) {
		stream_set_bw(a->strm, AUDIO_BANDWIDTH);
	}

	/* Audio codecs */
	for (le = list_head(aucodecl); le; le = le->next) {

		struct aucodec *ac = le->data;

		if (ac->ptime)
			minptime = min(minptime, ac->ptime);

		err = add_audio_codec(stream_sdpmedia(a->strm), ac);
		if (err)
			goto out;
	}

	err  = sdp_media_set_lattr(stream_sdpmedia(a->strm), true,
				   "minptime", "%u", minptime);
	err |= sdp_media_set_lattr(stream_sdpmedia(a->strm), true,
				   "ptime", "%u", ptime);
	if (err)
		goto out;

	if (cfg->audio.level && offerer) {

		a->extmap_aulevel = 1;

		err = sdp_media_set_lattr(stream_sdpmedia(a->strm), true,
					  "extmap",
					  "%u %s",
					  a->extmap_aulevel, uri_aulevel);
		if (err)
			goto out;
	}

	tx->mb = mbuf_alloc(STREAM_PRESZ + 4096);
	tx->sampv = mem_zalloc(AUDIO_SAMPSZ * aufmt_sample_size(tx->enc_fmt),
			       NULL);

	rx->sampv = mem_zalloc(AUDIO_SAMPSZ * aufmt_sample_size(rx->dec_fmt),
			       NULL);
	if (!tx->mb || !tx->sampv || !rx->sampv) {
		err = ENOMEM;
		goto out;
	}

	err = telev_alloc(&a->telev, ptime);
	if (err)
		goto out;

	err = add_telev_codec(a);
	if (err)
		goto out;

	auresamp_init(&tx->resamp);

	if (acc && acc->ausrc_mod) {

		tx->module = mem_ref(acc->ausrc_mod);
		tx->device = mem_ref(acc->ausrc_dev);

		info("audio: using account specific source: (%s,%s)\n",
		     tx->module, tx->device);
	}
	else {
		err  = str_dup(&tx->module, a->cfg.src_mod);
		err |= str_dup(&tx->device, a->cfg.src_dev);
	}

	tx->ptime  = ptime;
	tx->ts_ext = tx->ts_base = rand_u16();
	tx->marker = true;

	auresamp_init(&rx->resamp);

	if (acc && acc->auplay_mod) {

		rx->module = mem_ref(acc->auplay_mod);
		rx->device = mem_ref(acc->auplay_dev);

		info("audio: using account specific player: (%s,%s)\n",
		     rx->module, rx->device);
	}
	else {
		err  = str_dup(&rx->module, a->cfg.play_mod);
		err |= str_dup(&rx->device, a->cfg.play_dev);
	}

	rx->pt     = -1;
	rx->ptime  = ptime;

	a->eventh  = eventh;
	a->levelh  = levelh;
	a->errh    = errh;
	a->arg     = arg;

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
	uint64_t ts = 0;

	while (a->tx.u.thr.run) {

		uint64_t now;

		sys_msleep(4);

		if (!tx->aubuf_started)
			continue;

		if (!a->tx.u.thr.run)
			break;

		now = tmr_jiffies();
		if (!ts)
			ts = now;

		if (ts > now)
			continue;

		/* Now is the time to send */

		if (aubuf_cur_size(tx->aubuf) >= tx->psize) {

			poll_aubuf_tx(a);
		}
		else {
			++tx->stats.aubuf_underrun;

			debug("audio: thread: tx aubuf underrun"
			      " (total %llu)\n", tx->stats.aubuf_underrun);
		}

		ts += tx->ptime;
	}

	return NULL;
}
#endif


static void aufilt_param_set(struct aufilt_prm *prm,
			     const struct aucodec *ac, enum aufmt fmt)
{
	prm->srate      = ac->srate;
	prm->ch         = ac->ch;
	prm->fmt        = fmt;
}


static int autx_print_pipeline(struct re_printf *pf, const struct autx *autx)
{
	struct le *le;
	int err;

	if (!autx)
		return 0;

	err = re_hprintf(pf, "audio tx pipeline:  %10s",
			 autx->ausrc ? autx->ausrc->as->name : "(src)");

	for (le = list_head(&autx->filtl); le; le = le->next) {
		struct aufilt_enc_st *st = le->data;

		if (st->af->ench)
			err |= re_hprintf(pf, " ---> %s", st->af->name);
	}

	err |= re_hprintf(pf, " ---> %s\n",
			  autx->ac ? autx->ac->name : "(encoder)");

	return err;
}


static int aurx_print_pipeline(struct re_printf *pf, const struct aurx *aurx)
{
	struct le *le;
	int err;

	if (!aurx)
		return 0;

	err = re_hprintf(pf, "audio rx pipeline:  %10s",
			 aurx->auplay ? aurx->auplay->ap->name : "(play)");

	for (le = list_head(&aurx->filtl); le; le = le->next) {
		struct aufilt_dec_st *st = le->data;

		if (st->af->dech)
			err |= re_hprintf(pf, " <--- %s", st->af->name);
	}

	err |= re_hprintf(pf, " <--- %s\n",
			  aurx->ac ? aurx->ac->name : "(decoder)");

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
static int aufilt_setup(struct audio *a, struct list *aufiltl)
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

	aufilt_param_set(&encprm, tx->ac, tx->enc_fmt);
	aufilt_param_set(&decprm, rx->ac, rx->dec_fmt);

	/* Audio filters */
	for (le = list_head(aufiltl); le; le = le->next) {
		struct aufilt *af = le->data;
		struct aufilt_enc_st *encst = NULL;
		struct aufilt_dec_st *decst = NULL;
		void *ctx = NULL;

		if (af->encupdh) {
			err = af->encupdh(&encst, &ctx, af, &encprm, a);
			if (err) {
				warning("audio: error in encode audio-filter"
					" '%s' (%m)\n", af->name, err);
			}
			else {
				encst->af = af;
				list_append(&tx->filtl, &encst->le, encst);
			}
		}

		if (af->decupdh) {
			err = af->decupdh(&decst, &ctx, af, &decprm, a);
			if (err) {
				warning("audio: error in decode audio-filter"
					" '%s' (%m)\n", af->name, err);
			}
			else {
				decst->af = af;
				list_append(&rx->filtl, &decst->le, decst);
			}
		}

		if (err) {
			warning("audio: audio-filter '%s'"
				" update failed (%m)\n", af->name, err);
			break;
		}
	}

	return 0;
}


static int start_player(struct aurx *rx, struct audio *a,
			struct list *auplayl)
{
	const struct aucodec *ac = rx->ac;
	uint32_t srate_dsp;
	uint32_t channels_dsp;
	bool resamp = false;
	int err;

	if (!ac)
		return 0;

	srate_dsp    = ac->srate;
	channels_dsp = ac->ch;

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
		     ac->srate, ac->ch, srate_dsp, channels_dsp);

		rx->sampv_rs = mem_zalloc(AUDIO_SAMPSZ * sizeof(int16_t),
					  NULL);
		if (!rx->sampv_rs)
			return ENOMEM;

		err = auresamp_setup(&rx->resamp,
				     ac->srate, ac->ch,
				     srate_dsp, channels_dsp);
		if (err) {
			warning("audio: could not setup auplay resampler"
				" (%m)\n", err);
			return err;
		}
	}

	/* Start Audio Player */
	if (!rx->auplay && auplay_find(auplayl, NULL)) {

		struct auplay_prm prm;

		prm.srate      = srate_dsp;
		prm.ch         = channels_dsp;
		prm.ptime      = rx->ptime;
		prm.fmt        = rx->play_fmt;

		if (!rx->aubuf) {
			const size_t sz = aufmt_sample_size(rx->play_fmt);
			const size_t ptime_min = a->cfg.buffer.min;
			const size_t ptime_max = a->cfg.buffer.max;
			size_t min_sz;
			size_t max_sz;

			if (!ptime_min || !ptime_max)
				return EINVAL;

			min_sz = sz*calc_nsamp(prm.srate, prm.ch, ptime_min);
			max_sz = sz*calc_nsamp(prm.srate, prm.ch, ptime_max);

			debug("audio: create recv buffer"
			      " [%zu - %zu ms]"
			      " [%zu - %zu bytes]\n",
			      ptime_min, ptime_max, min_sz, max_sz);

			err = aubuf_alloc(&rx->aubuf, min_sz, max_sz);
			if (err) {
				warning("audio: aubuf alloc error (%m)\n",
					err);
				return err;
			}

			rx->aubuf_maxsz = max_sz;
		}

		err = auplay_alloc(&rx->auplay, auplayl,
				   rx->module,
				   &prm, rx->device,
				   auplay_write_handler, rx);
		if (err) {
			warning("audio: start_player failed (%s.%s): %m\n",
				rx->module, rx->device, err);
			return err;
		}

		rx->auplay_prm = prm;

		info("audio: player started with sample format %s\n",
		     aufmt_name(rx->play_fmt));
	}

	return 0;
}


static int start_source(struct autx *tx, struct audio *a, struct list *ausrcl)
{
	const struct aucodec *ac = tx->ac;
	uint32_t srate_dsp;
	uint32_t channels_dsp;
	bool resamp = false;
	int err;

	if (!ac)
		return 0;

	srate_dsp    = ac->srate;
	channels_dsp = ac->ch;

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
		     ac->srate, ac->ch, srate_dsp, channels_dsp);

		tx->sampv_rs = mem_zalloc(AUDIO_SAMPSZ * sizeof(int16_t),
					  NULL);
		if (!tx->sampv_rs)
			return ENOMEM;

		err = auresamp_setup(&tx->resamp,
				     srate_dsp, channels_dsp,
				     ac->srate, ac->ch);
		if (err) {
			warning("audio: could not setup ausrc resampler"
				" (%m)\n", err);
			return err;
		}
	}

	/* Start Audio Source */
	if (!tx->ausrc && ausrc_find(ausrcl, NULL) && !a->hold) {

		struct ausrc_prm prm;
		size_t sz;

		prm.srate      = srate_dsp;
		prm.ch         = channels_dsp;
		prm.ptime      = tx->ptime;
		prm.fmt        = tx->src_fmt;

		sz = aufmt_sample_size(tx->src_fmt);

		tx->psize = sz * calc_nsamp(prm.srate, prm.ch, prm.ptime);

		tx->aubuf_maxsz = tx->psize * 30;

		if (!tx->aubuf) {
			err = aubuf_alloc(&tx->aubuf, tx->psize,
					  tx->aubuf_maxsz);
			if (err)
				return err;
		}

		err = ausrc_alloc(&tx->ausrc, ausrcl,
				  tx->ctx, tx->module,
				  &prm, tx->device,
				  ausrc_read_handler, ausrc_error_handler, a);
		if (err) {
			warning("audio: start_source failed (%s.%s): %m\n",
				tx->module, tx->device, err);
			return err;
		}

		switch (a->cfg.txmode) {

		case AUDIO_MODE_POLL:
			break;

#ifdef HAVE_PTHREAD
		case AUDIO_MODE_THREAD:
			if (!tx->u.thr.run) {
				tx->u.thr.run = true;
				err = pthread_create(&tx->u.thr.tid, NULL,
						     tx_thread, a);
				if (err) {
					tx->u.thr.run = false;
					return err;
				}
			}
			break;
#endif

		default:
			warning("audio: tx mode not supported (%d)\n",
				a->cfg.txmode);
			return ENOTSUP;
		}

		tx->ausrc_prm = prm;

		info("audio: source started with sample format %s\n",
		     aufmt_name(tx->src_fmt));
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
	struct list *aufiltl = baresip_aufiltl();
	int err;

	if (!a)
		return EINVAL;

	debug("audio: start\n");

	/* Audio filter */
	if (!list_isempty(aufiltl)) {

		err = aufilt_setup(a, aufiltl);
		if (err)
			return err;
	}

	err  = start_player(&a->rx, a, baresip_auplayl());
	err |= start_source(&a->tx, a, baresip_ausrcl());
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
 * Start the audio source
 *
 * @param a       Audio object
 * @param ausrcl  List of audio sources
 * @param aufiltl List of audio filters
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_start_source(struct audio *a, struct list *ausrcl,
		       struct list *aufiltl)
{
	int err;

	if (!a)
		return EINVAL;

	/* NOTE: audio encoder must be set first */
	if (!a->tx.ac) {
		warning("audio: start_source: no encoder set\n");
		return ENOENT;
	}

	/* Audio filter */
	if (!list_isempty(aufiltl)) {

		err = aufilt_setup(a, aufiltl);
		if (err)
			return err;
	}

	err = start_source(&a->tx, a, ausrcl);
	if (err)
		return err;

	a->started = true;

	return 0;
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


/**
 * Check if audio has been started
 *
 * @param a Audio object
 *
 * @return True if audio has been started, otherwise false
 */
bool audio_started(const struct audio *a)
{
	if (!a)
		return false;

	return a->started;
}


/**
 * Set the audio encoder used
 *
 * @param a      Audio object
 * @param ac     Audio codec to use
 * @param pt_tx  Payload type for sending
 * @param params Optional encoder parameters
 *
 * @return 0 if success, otherwise errorcode
 */
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
		     ac->name, ac->srate, ac->ch);

		/* Audio source must be stopped first */
		if (reset) {
			tx->ausrc = mem_deref(tx->ausrc);
			aubuf_flush(tx->aubuf);
		}

		tx->enc = mem_deref(tx->enc);
		tx->ac = ac;
	}

	if (ac->encupdh) {
		struct auenc_param prm;

		prm.ptime = tx->ptime;
		prm.bitrate = 0;        /* auto */

		err = ac->encupdh(&tx->enc, ac, &prm, params);
		if (err) {
			warning("audio: alloc encoder: %m\n", err);
			return err;
		}
	}

	stream_set_srate(a->strm, ac->crate, 0);
	stream_update_encoder(a->strm, pt_tx);

	telev_set_srate(a->telev, ac->crate);

	/* use a codec-specific ptime */
	if (ac->ptime) {
		const size_t sz = aufmt_sample_size(tx->src_fmt);

		tx->ptime = ac->ptime;
		tx->psize = sz * calc_nsamp(ac->srate, ac->ch, ac->ptime);
	}

	if (!tx->ausrc) {
		err |= audio_start(a);
	}

	return err;
}


/**
 * Set the audio decoder used
 *
 * @param a      Audio object
 * @param ac     Audio codec to use
 * @param pt_rx  Payload type for receiving
 * @param params Optional decoder parameters
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_decoder_set(struct audio *a, const struct aucodec *ac,
		      int pt_rx, const char *params)
{
	struct aurx *rx;
	struct sdp_media *m;
	bool reset = false;
	int err = 0;

	if (!a || !ac)
		return EINVAL;

	rx = &a->rx;

	reset = !aucodec_equal(ac, rx->ac);
	m = stream_sdpmedia(audio_strm(a));
	reset |= sdp_media_dir(m)!=SDP_SENDRECV;

	if (ac != rx->ac) {

		info("audio: Set audio decoder: %s %uHz %dch\n",
		     ac->name, ac->srate, ac->ch);

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

	stream_set_srate(a->strm, 0, ac->crate);

	if (reset) {
		stream_reset(audio_strm(a));

		rx->auplay = mem_deref(rx->auplay);
		aubuf_flush(rx->aubuf);

		/* Reset audio filter chain */
		list_flush(&rx->filtl);

		err |= audio_start(a);
	}

	return err;
}


/**
 * Get the RTP Stream object from an Audio object
 *
 * @param au  Audio object
 *
 * @return RTP Stream object
 */
struct stream *audio_strm(const struct audio *au)
{
	return au ? au->strm : NULL;
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


static bool extmap_handler(const char *name, const char *value, void *arg)
{
	struct audio *au = arg;
	struct sdp_extmap extmap;
	int err;
	(void)name;

	MAGIC_CHECK(au);

	err = sdp_extmap_decode(&extmap, value);
	if (err) {
		warning("audio: sdp_extmap_decode error (%m)\n", err);
		return false;
	}

	if (0 == pl_strcasecmp(&extmap.name, uri_aulevel)) {

		if (extmap.id < RTPEXT_ID_MIN || extmap.id > RTPEXT_ID_MAX) {
			warning("audio: extmap id out of range (%u)\n",
				extmap.id);
			return false;
		}

		au->extmap_aulevel = extmap.id;

		err = sdp_media_set_lattr(stream_sdpmedia(au->strm), true,
					  "extmap",
					  "%u %s",
					  au->extmap_aulevel,
					  uri_aulevel);
		if (err)
			return false;

		au->level_enabled = true;
		info("audio: client-to-mixer audio levels enabled\n");
	}

	return false;
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

		if (ptime_tx && ptime_tx != a->tx.ptime
		    && ptime_tx <= MAX_PTIME) {

			info("audio: peer changed ptime_tx %ums -> %ums\n",
			     a->tx.ptime, ptime_tx);

			tx->ptime = ptime_tx;

			if (tx->ac) {
				size_t sz;

				sz = aufmt_sample_size(tx->src_fmt);

				tx->psize = sz * calc_nsamp(tx->ac->srate,
							    tx->ac->ch,
							    ptime_tx);
			}

			sdp_media_set_lattr(stream_sdpmedia(a->strm), true,
					    "ptime", "%u", ptime_tx);
		}
	}

	/* Client-to-Mixer Audio Level Indication */
	if (a->cfg.level) {
		sdp_media_rattr_apply(stream_sdpmedia(a->strm),
				      "extmap",
				      extmap_handler, a);
	}
}


/**
 * Put an audio level value, call the level handler
 *
 * @param au  Audio object
 * @param tx  Direction; true for transmit, false for receive
 * @param lvl Audio level value
 */
void audio_level_put(const struct audio *au, bool tx, double lvl)
{
	if (!au)
		return;

	if (au->levelh)
		au->levelh(tx, lvl, au->arg);
}


/**
 * Get the last value of the audio level from incoming RTP packets
 *
 * @param au     Audio object
 * @param levelp Pointer to where to write audio level value
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_level_get(const struct audio *au, double *levelp)
{
	if (!au)
		return EINVAL;

	if (!au->level_enabled)
		return ENOTSUP;

	if (!au->rx.level_set)
		return ENOENT;

	if (levelp)
		*levelp = au->rx.level_last;

	return 0;
}


static int aucodec_print(struct re_printf *pf, const struct aucodec *ac)
{
	if (!ac)
		return 0;

	return re_hprintf(pf, "%s %uHz/%dch",
			  ac->name, ac->srate, ac->ch);
}


/**
 * Print the audio debug information
 *
 * @param pf   Print function
 * @param a    Audio object
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_debug(struct re_printf *pf, const struct audio *a)
{
	const struct autx *tx;
	const struct aurx *rx;
	size_t sztx, szrx;
	int err;

	if (!a)
		return 0;

	tx = &a->tx;
	rx = &a->rx;

	sztx = aufmt_sample_size(tx->src_fmt);
	szrx = aufmt_sample_size(rx->play_fmt);

	err  = re_hprintf(pf, "\n--- Audio stream ---\n");

	err |= re_hprintf(pf, " tx:   encode: %H ptime=%ums %s\n",
			  aucodec_print, tx->ac,
			  tx->ptime,
			  aufmt_name(tx->enc_fmt));
	err |= re_hprintf(pf, "       aubuf: %H"
			  " (cur %.2fms, max %.2fms, or %llu, ur %llu)\n",
			  aubuf_debug, tx->aubuf,
			  calc_ptime(aubuf_cur_size(tx->aubuf)/sztx,
				     tx->ausrc_prm.srate,
				     tx->ausrc_prm.ch),
			  calc_ptime(tx->aubuf_maxsz/sztx,
				     tx->ausrc_prm.srate,
				     tx->ausrc_prm.ch),
			  tx->stats.aubuf_overrun,
			  tx->stats.aubuf_underrun);
	err |= re_hprintf(pf, "       source: %s,%s %s\n",
			  tx->ausrc ? tx->ausrc->as->name : "none",
			  tx->device,
			  aufmt_name(tx->src_fmt));
	err |= re_hprintf(pf, "       time = %.3f sec\n",
			  autx_calc_seconds(tx));

	err |= re_hprintf(pf,
			  " rx:   decode: %H %s\n",
			  aucodec_print, rx->ac, aufmt_name(rx->dec_fmt));
	err |= re_hprintf(pf, "       aubuf: %H"
			  " (cur %.2fms, max %.2fms, or %llu, ur %llu)\n",
			  aubuf_debug, rx->aubuf,
			  calc_ptime(aubuf_cur_size(rx->aubuf)/szrx,
				     rx->auplay_prm.srate,
				     rx->auplay_prm.ch),
			  calc_ptime(rx->aubuf_maxsz/szrx,
				     rx->auplay_prm.srate,
				     rx->auplay_prm.ch),
			  rx->stats.aubuf_overrun,
			  rx->stats.aubuf_underrun
			  );
	err |= re_hprintf(pf, "       player: %s,%s %s\n",
			  rx->auplay ? rx->auplay->ap->name : "none",
			  rx->device,
			  aufmt_name(rx->play_fmt));
	err |= re_hprintf(pf, "       n_discard:%llu\n",
			  rx->stats.n_discard);
	if (rx->level_set) {
		err |= re_hprintf(pf, "       level %.3f dBov\n",
				  rx->level_last);
	}
	if (rx->ts_recv.is_set) {
		err |= re_hprintf(pf, "       time = %.3f sec\n",
				  aurx_calc_seconds(rx));
	}
	else {
		err |= re_hprintf(pf, "       time = (not started)\n");
	}

	err |= re_hprintf(pf,
			  " %H"
			  " %H",
			  autx_print_pipeline, tx,
			  aurx_print_pipeline, rx);

	err |= stream_debug(pf, a->strm);

	return err;
}


/**
 * Set the audio source and player device name. This function does not
 * change the state of the audio source/player.
 *
 * @param a     Audio object
 * @param src   Audio source device name
 * @param play  Audio player device name
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_set_devicename(struct audio *a, const char *src, const char *play)
{
	int err;

	if (!a)
		return EINVAL;

	a->tx.device = mem_deref(a->tx.device);
	a->rx.device = mem_deref(a->rx.device);

	err  = str_dup(&a->tx.device, src);
	err |= str_dup(&a->rx.device, play);

	return err;
}


/**
 * Set the audio source state to a new audio source module and device.
 * The current audio source will be stopped.
 *
 * @param au     Audio object
 * @param mod    Audio source module (NULL to stop)
 * @param device Audio source device name
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_set_source(struct audio *au, const char *mod, const char *device)
{
	struct autx *tx;
	int err;

	if (!au)
		return EINVAL;

	tx = &au->tx;

	/* stop the audio device first */
	tx->ausrc = mem_deref(tx->ausrc);

	if (str_isset(mod)) {

		err = ausrc_alloc(&tx->ausrc, baresip_ausrcl(),
				  NULL, mod, &tx->ausrc_prm, device,
				  ausrc_read_handler, ausrc_error_handler, au);
		if (err) {
			warning("audio: set_source failed (%s.%s): %m\n",
				mod, device, err);
			return err;
		}
	}

	return 0;
}


/**
 * Set the audio player state to a new audio player module and device.
 * The current audio player will be stopped.
 *
 * @param au     Audio object
 * @param mod    Audio player module (NULL to stop)
 * @param device Audio player device name
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_set_player(struct audio *au, const char *mod, const char *device)
{
	struct aurx *rx;
	int err;

	if (!au)
		return EINVAL;

	rx = &au->rx;

	/* stop the audio device first */
	rx->auplay = mem_deref(rx->auplay);

	if (str_isset(mod)) {

		err = auplay_alloc(&rx->auplay, baresip_auplayl(),
				   mod, &rx->auplay_prm, device,
				   auplay_write_handler, rx);
		if (err) {
			warning("audio: set_player failed (%s.%s): %m\n",
				mod, device, err);
			return err;
		}
	}

	return 0;
}


/**
 * Set the bitrate for the audio encoder
 *
 * @param au      Audio object
 * @param bitrate Encoder bitrate in [bits/s]
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_set_bitrate(struct audio *au, uint32_t bitrate)
{
	struct autx *tx;
	const struct aucodec *ac;
	int err;

	if (!au)
		return EINVAL;

	tx = &au->tx;

	ac = tx->ac;

	info("audio: set bitrate for encoder '%s' to %u bits/s\n",
	     ac ? ac->name : "?",
	     bitrate);

	if (ac) {

		if (ac->encupdh) {
			struct auenc_param prm;

			prm.ptime = tx->ptime;
			prm.bitrate = bitrate;

			err = ac->encupdh(&tx->enc, ac, &prm, NULL);
			if (err) {
				warning("audio: encupdh error: %m\n", err);
				return err;
			}
		}

	}
	else {
		info("audio: set_bitrate: no audio encoder\n");
	}

	return 0;
}


/**
 * Check if audio receiving has started
 *
 * @param au      Audio object
 *
 * @return True if started, otherwise false
 */
bool audio_rxaubuf_started(const struct audio *au)
{
	const struct aurx *rx;

	if (!au)
		return false;

	rx = &au->rx;

	return rx->aubuf_started;
}


/**
 * Set the audio stream on hold
 *
 * @param au    Audio object
 * @param hold  True to hold, false to resume
 */
void audio_set_hold(struct audio *au, bool hold)
{
	if (!au)
		return;

	au->hold = hold;
}


/**
 * Get audio codec of audio stream
 *
 * @param au    Audio object
 * @param tx    True to get transmit codec, false to get receive codec
 *
 * @return      Audio codec if success, otherwise NULL
 */
const struct aucodec *audio_codec(const struct audio *au, bool tx)
{
	if (!au)
		return NULL;

	return tx ? au->tx.ac : au->rx.ac;
}


/**
 * Accessor function to audio configuration
 *
 * @param au Audio object
 *
 * @return Audio configuration
 */
struct config_audio *audio_config(struct audio *au)
{
	return au ? &au->cfg : NULL;
}


/**
 * Set the media context
 *
 * @param au  Audio object
 * @param ctx Media context
 */
void audio_set_media_context(struct audio *au, struct media_ctx **ctx)
{
	if (!au)
		return;

	au->tx.ctx = ctx;
}
