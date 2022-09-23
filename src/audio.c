/**
 * @file src/audio.c  Audio stream
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 * \ref GenericAudioStream
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _POSIX_C_SOURCE 199309L
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <time.h>
#include <re.h>
#include <re_atomic.h>
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

 .    .-------.   .-------.   .--------.   .--------.
 |    |       |   |       |   |        |   |        |
 |O-->| ausrc |-->| aubuf |-->| aufilt |-->| encode |---> RTP
 |    |       |   |       |   |        |   |        |
 '    '-------'   '-------'   '--------'   '--------'

 \endverbatim
 *
 */
struct autx {
	const struct ausrc *as;       /**< Audio Source module             */
	struct ausrc_st *ausrc;       /**< Audio Source state              */
	struct ausrc_prm ausrc_prm;   /**< Audio Source parameters         */
	const struct aucodec *ac;     /**< Current audio encoder           */
	struct auenc_state *enc;      /**< Audio encoder state (optional)  */
	struct aubuf *aubuf;          /**< Packetize outgoing stream       */
	size_t aubuf_maxsz;           /**< Maximum aubuf size in [bytes]   */
	volatile bool aubuf_started;  /**< Aubuf was started flag          */
	struct list filtl;            /**< Audio filters in encoding order */
	struct mbuf *mb;              /**< Buffer for outgoing RTP packets */
	char *module;                 /**< Audio source module name        */
	char *device;                 /**< Audio source device name        */
	void *sampv;                  /**< Sample buffer                   */
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

	struct {
		uint64_t aubuf_overrun;
		uint64_t aubuf_underrun;
	} stats;

	struct {
		thrd_t tid;           /**< Audio transmit thread           */
		RE_ATOMIC bool run;   /**< Audio transmit thread running   */
	} thr;

	mtx_t *mtx;
};


/**
 * Audio receive/decoder
 *
 \verbatim

 Processing decoder pipeline:

       .--------.   .-------.   .--------.   .--------.
 |\    |        |   |       |   |        |   |        |
 | |<--| auplay |<--| aubuf |<--| aufilt |<--| decode |<--- RTP
 |/    |        |   |       |   |        |   |        |
       '--------'   '-------'   '--------'   '--------'

 \endverbatim
 */
struct aurx {
	const struct auplay *ap;
	struct auplay_st *auplay;     /**< Audio Player                    */
	struct auplay_prm auplay_prm; /**< Audio Player parameters         */
	const struct aucodec *ac;     /**< Current audio decoder           */
	struct audec_state *dec;      /**< Audio decoder state (optional)  */
	struct aubuf *aubuf;          /**< Audio buffer before auplay      */
	uint32_t ssrc;                /**< Incoming synchronization source */
	size_t aubuf_minsz;           /**< Minimum aubuf size in [bytes]   */
	size_t aubuf_maxsz;           /**< Maximum aubuf size in [bytes]   */
	size_t num_bytes;             /**< Size of one frame in [bytes]    */
	volatile bool aubuf_started;  /**< Aubuf was started flag          */
	struct list filtl;            /**< Audio filters in decoding order */
	char *module;                 /**< Audio player module name        */
	char *device;                 /**< Audio player device name        */
	void *sampv;                  /**< Sample buffer                   */
	uint32_t ptime;               /**< Packet time for receiving       */
	int pt;                       /**< Payload type for incoming RTP   */
	double level_last;            /**< Last audio level value [dBov]   */
	bool level_set;               /**< True if level_last is set       */
	enum aufmt play_fmt;          /**< Sample format for audio playback*/
	enum aufmt dec_fmt;           /**< Sample format for decoder       */
	struct timestamp_recv ts_recv;/**< Receive timestamp state         */

	size_t last_sampc;

	struct {
		uint64_t aubuf_overrun;
		uint64_t aubuf_underrun;
		uint64_t n_discard;
	} stats;

	enum jbuf_type jbtype;       /**< Jitter buffer type               */
	volatile int32_t wcnt;       /**< Write handler call count         */

	mtx_t *mtx;

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
	bool conference;              /**< Local conference flag           */
	uint8_t extmap_aulevel;       /**< ID Range 1-14 inclusive         */
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

	if (a->cfg.txmode == AUDIO_MODE_THREAD &&
	    re_atomic_rlx(&tx->thr.run)) {
		re_atomic_rlx_set(&tx->thr.run, false);
		thrd_join(tx->thr.tid, NULL);
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
	if (rx->mtx)
		mtx_lock(rx->mtx);

	list_flush(&rx->filtl);
	if (rx->mtx)
		mtx_unlock(rx->mtx);
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
	mem_deref(a->tx.module);
	mem_deref(a->tx.device);
	mem_deref(a->rx.module);
	mem_deref(a->rx.device);

	list_flush(&a->tx.filtl);

	mem_deref(a->strm);
	mem_deref(a->telev);

	mem_deref(a->tx.mtx);
	mem_deref(a->rx.mtx);
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
			    struct auframe *af)
{
	struct bundle *bun = stream_bundle(a->strm);
	bool bundled = bundle_state(bun) != BUNDLE_NONE;
	size_t frame_size;  /* number of samples per channel */
	size_t sampc_rtp;
	size_t len;
	size_t ext_len = 0;
	uint32_t ts_delta = 0;
	bool marker = tx->marker;
	int err;

	if (!tx->ac || !tx->ac->ench)
		return;

	if (tx->ac->srate != af->srate || tx->ac->ch != af->ch) {
		warning("audio: srate/ch of frame %u/%u vs audio codec %u/%u. "
			"Use module auresamp!\n",
			af->srate, af->ch, tx->ac->srate, tx->ac->ch);
		return;
	}

	tx->mb->pos = tx->mb->end = STREAM_PRESZ;

	if (a->level_enabled || bundled) {

		/* skip the extension header */
		tx->mb->pos += RTPEXT_HDR_SIZE;

		if (a->level_enabled) {
			err = append_rtpext(a, tx->mb, af->fmt,
					    af->sampv, af->sampc);
			if (err)
				return;
		}

		if (bundled) {
			const char *mid = stream_mid(a->strm);

			rtpext_encode(tx->mb, bundle_extmap_mid(bun),
				      str_len(mid), (void *)mid);
		}

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
			   af->fmt, af->sampv, af->sampc);

	if ((err & 0xffff0000) == 0x00010000) {

		/* MPA needs some special treatment here */

		ts_delta = err & 0xffff;
		af->sampc = 0;
	}
	else if (err) {
		warning("audio: %s encode error: %d samples (%m)\n",
			tx->ac->name, af->sampc, err);
		goto out;
	}

	tx->mb->pos = STREAM_PRESZ;
	tx->mb->end = STREAM_PRESZ + ext_len + len;

	if (mbuf_get_left(tx->mb)) {

		uint32_t rtp_ts = tx->ts_ext & 0xffffffff;

		if (len) {
			mtx_lock(a->tx.mtx);
			err = stream_send(a->strm, ext_len!=0, marker, -1,
					  rtp_ts, tx->mb);
			mtx_unlock(a->tx.mtx);
			if (err)
				goto out;
		}

		if (ts_delta) {
			tx->ts_ext += ts_delta;
			goto out;
		}
	}

	/* Convert from audio samplerate to RTP clockrate */
	sampc_rtp = af->sampc * tx->ac->crate / tx->ac->srate;

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
	struct le *le;
	uint32_t srate;
	uint8_t ch;
	int err = 0;

	sz = aufmt_sample_size(tx->src_fmt);
	if (!sz)
		return;

	sampc = tx->psize / sz;
	srate = tx->ausrc_prm.srate;
	ch = tx->ausrc_prm.ch;

	/* timed read from audio-buffer */
	auframe_init(&af, tx->src_fmt, sampv, sampc, srate, ch);
	aubuf_read_auframe(tx->aubuf, &af);

	/* Process exactly one audio-frame in list order */
	for (le = tx->filtl.head; le; le = le->next) {
		struct aufilt_enc_st *st = le->data;

		if (st->af && st->af->ench)
			err |= st->af->ench(st, &af);
	}
	if (err) {
		warning("audio: aufilter encode: %m\n", err);
	}

	if (af.fmt != tx->enc_fmt) {
		warning("audio: tx: invalid sample formats (%s -> %s). %s\n",
			aufmt_name(af.fmt), aufmt_name(tx->enc_fmt),
			tx->enc_fmt == AUFMT_S16LE ? "Use module auconv!" : ""
			);
	}

	/* Encode and send */
	encode_rtp_send(a, tx, &af);
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

	mtx_lock(tx->mtx);
	err = telev_poll(a->telev, &marker, mb);
	mtx_unlock(tx->mtx);
	if (err)
		goto out;

	if (marker)
		tx->ts_tel = (uint32_t)tx->ts_ext;

	fmt = sdp_media_rformat(stream_sdpmedia(audio_strm(a)), telev_rtpfmt);
	if (!fmt)
		goto out;

	mb->pos = STREAM_PRESZ;
	mtx_lock(a->tx.mtx);
	err = stream_send(a->strm, false, marker, fmt->pt, tx->ts_tel, mb);
	mtx_unlock(a->tx.mtx);
	if (err) {
		warning("audio: telev: stream_send %m\n", err);
	}

 out:
	mem_deref(mb);
}


bool audio_txtelev_empty(const struct audio *au)
{
	const struct autx *tx;
	bool empty;

	if (!au)
		return true;

	tx = &au->tx;
	mtx_lock(tx->mtx);
	empty = telev_is_empty(au->telev);
	mtx_unlock(tx->mtx);
	return empty;
}


/*
 * Write samples to Audio Player. This version of the write handler is used
 * for the configuration jitter_buffer_type JBUF_FIXED.
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
static void auplay_write_handler(struct auframe *af, void *arg)
{
	struct audio *a = arg;
	struct aurx *rx = &a->rx;
	size_t num_bytes = auframe_size(af);

	mtx_lock(rx->mtx);
	if (rx->aubuf_started && aubuf_cur_size(rx->aubuf) < num_bytes) {

		++rx->stats.aubuf_underrun;

#if 0
		debug("audio: rx aubuf underrun (total %llu)\n",
			rx->stats.aubuf_underrun);
#endif
	}

	mtx_unlock(rx->mtx);
	aubuf_read_auframe(rx->aubuf, af);
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
	enum aufmt fmt;
	unsigned i;

	mtx_lock(tx->mtx);
	fmt = tx->src_fmt;
	mtx_unlock(tx->mtx);

	if (fmt != af->fmt) {
		warning("audio: ausrc format mismatch:"
			" expected=%d(%s), actual=%d(%s)\n",
			fmt, aufmt_name(tx->src_fmt),
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

	(void)aubuf_write_auframe(tx->aubuf, af);

	mtx_lock(tx->mtx);
	tx->aubuf_started = true;
	mtx_unlock(tx->mtx);

	if (a->cfg.txmode != AUDIO_MODE_POLL)
		return;

	for (i=0; i<16; i++) {
		if (aubuf_cur_size(tx->aubuf) < tx->psize)
			break;

		poll_aubuf_tx(a);
	}

	/* Exact timing: send Telephony-Events from here */
	check_telev(a, tx);
}


static void ausrc_error_handler(int err, const char *str, void *arg)
{
	struct audio *a = arg;
	MAGIC_CHECK(a);

	if (!err) {
		info("audio: ausrc - %s\n", str);
	}
	else {
		if (a->errh)
			a->errh(err, str, a->arg);
	}
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


static bool audio_is_telev(struct audio *a, int pt)
{
	const struct sdp_format *lc;

	lc = sdp_media_lformat(stream_sdpmedia(a->strm), pt);
	return  lc && !str_casecmp(lc->name, "telephone-event");
}


static int stream_pt_handler(uint8_t pt, struct mbuf *mb, void *arg)
{
	struct audio *a = arg;
	const struct sdp_format *lc;
	struct aurx *rx = &a->rx;

	if (!rx || rx->pt == pt)
		return 0;

	lc = sdp_media_lformat(stream_sdpmedia(a->strm), pt);

	/* Telephone event? */
	if (lc && !str_casecmp(lc->name, "telephone-event")) {
		handle_telev(a, mb);
		return ENODATA;
	}

	if (!lc)
		return ENOENT;

	if (rx->pt != -1)
		info("Audio decoder changed payload %d -> %u\n", rx->pt, pt);

	a->rx.pt = pt;
	return audio_decoder_set(a, lc->data, lc->pt, lc->params);
}


static int process_decfilt(struct aurx *rx, struct auframe *af)
{
	int err = 0;

	/* Process exactly one audio-frame in reverse list order */
	mtx_lock(rx->mtx);
	for (struct le *le = rx->filtl.tail; le; le = le->prev) {
		struct aufilt_dec_st *st = le->data;

		if (st->af && st->af->dech)
			err = st->af->dech(st, af);

		if (err)
			break;
	}

	mtx_unlock(rx->mtx);
	return err;
}


static int rx_push_aubuf(struct aurx *rx, const struct auframe *af)
{
	int err;

	if (aubuf_cur_size(rx->aubuf) >= rx->aubuf_maxsz) {

		++rx->stats.aubuf_overrun;

#if 0
		debug("audio: rx aubuf overrun (total %llu)\n",
		      rx->stats.aubuf_overrun);
#endif
	}

	if (rx->auplay_prm.srate != af->srate || rx->auplay_prm.ch != af->ch) {
		warning("audio: srate/ch of frame %u/%u vs player %u/%u. Use "
			"module auresamp!\n",
			af->srate, af->ch,
			rx->auplay_prm.srate, rx->auplay_prm.ch);
	}

	if (af->fmt != rx->play_fmt) {
		warning("audio: rx: invalid sample formats (%s -> %s). %s\n",
			aufmt_name(af->fmt), aufmt_name(rx->play_fmt),
			rx->play_fmt == AUFMT_S16LE ? "Use module auconv!" : ""
		       );
	}

	err = aubuf_write_auframe(rx->aubuf, af);
	if (err)
		return err;

	mtx_lock(rx->mtx);
	if (!rx->aubuf_started &&
	    (aubuf_cur_size(rx->aubuf) >= rx->aubuf_minsz))
		rx->aubuf_started = true;

	mtx_unlock(rx->mtx);

	return 0;
}


static int aurx_stream_decode(struct aurx *rx, const struct rtp_header *hdr,
			      struct mbuf *mb, unsigned lostc, bool drop)
{
	struct auframe af;
	size_t sampc = AUDIO_SAMPSZ;
	bool marker = hdr->m;
	int err = 0;
	const struct aucodec *ac = rx->ac;
	bool flush = rx->ssrc != hdr->ssrc;

	/* No decoder set */
	if (!ac)
		return 0;

	rx->ssrc = hdr->ssrc;

	/* TODO: PLC */
	if (lostc && ac->plch) {

		err = ac->plch(rx->dec,
				   rx->dec_fmt, rx->sampv, &sampc,
				   mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio: %s codec decode %u bytes: %m\n",
				ac->name, mbuf_get_left(mb), err);
			goto out;
		}
	}
	else if (mbuf_get_left(mb)) {

		err = ac->dech(rx->dec,
				   rx->dec_fmt, rx->sampv, &sampc,
				   marker, mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio: %s codec decode %u bytes: %m\n",
				ac->name, mbuf_get_left(mb), err);
			goto out;
		}

		rx->last_sampc = sampc;
	}
	else {
		/* no PLC in the codec, might be done in filters below */
		sampc = 0;
	}

	auframe_init(&af, rx->dec_fmt, rx->sampv, sampc, ac->srate, ac->ch);
	af.timestamp = ((uint64_t) hdr->ts) * AUDIO_TIMEBASE / ac->crate;

	if (drop) {
		aubuf_drop_auframe(rx->aubuf, &af);
		goto out;
	}

	if (flush)
		aubuf_flush(rx->aubuf);

	err = process_decfilt(rx, &af);
	if (err)
		goto out;

	if (!rx->aubuf)
		goto out;

	err = rx_push_aubuf(rx, &af);
 out:
	return err;
}


/* Handle incoming stream data from the network */
static void stream_recv_handler(const struct rtp_header *hdr,
				struct rtpext *extv, size_t extc,
				struct mbuf *mb, unsigned lostc, bool *ignore,
				void *arg)
{
	struct audio *a = arg;
	struct aurx *rx = &a->rx;
	bool discard = false;
	bool drop = *ignore;
	size_t i;
	int wrap;
	(void) lostc;

	MAGIC_CHECK(a);

	if (!mb)
		goto out;

	if (audio_is_telev(a, hdr->pt)) {
		*ignore = true;
		return;
	}

	*ignore = false;
	/* RFC 5285 -- A General Mechanism for RTP Header Extensions */
	for (i=0; i<extc; i++) {

		if (extv[i].id == a->extmap_aulevel) {

			a->rx.level_last = -(double)(extv[i].data[0] & 0x7f);
			a->rx.level_set = true;
		}
	}

	/* Save timestamp for incoming RTP packets */

	if (!rx->ts_recv.is_set)
		timestamp_set(&rx->ts_recv, hdr->ts);

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
	/* TODO:  what if lostc > 1 ?*/
	/* PLC should generate lostc frames here. Not only one.
	 * aubuf should replace PLC frames with late arriving real frames.
	 * It should use timestamp to decide if a frame should be replaced. */
/*        if (lostc)*/
/*                (void)aurx_stream_decode(&a->rx, hdr, mb, lostc, drop);*/

	(void)aurx_stream_decode(&a->rx, hdr, mb, 0, drop);
}


static int add_telev_codec(struct audio *a)
{
	struct sdp_media *m = stream_sdpmedia(audio_strm(a));
	struct sdp_format *sf;
	uint32_t pt = a->cfg.telev_pt;
	char pts[11];
	int err;

	(void)re_snprintf(pts, sizeof(pts), "%u", pt);

	/* Use payload-type 101 if available, for CiscoGW interop */
	err = sdp_format_add(&sf, m, false,
			     (!sdp_media_lformat(m, pt)) ? pts : NULL,
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
		struct account *acc, struct sdp_session *sdp_sess,
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
	rx->jbtype  = cfg->avt.jbtype;

	err = stream_alloc(&a->strm, streaml,
			   stream_prm, &cfg->avt, sdp_sess,
			   MEDIA_AUDIO,
			   mnat, mnat_sess, menc, menc_sess, offerer,
			   stream_recv_handler, NULL, stream_pt_handler, a);
	if (err)
		goto out;

	if (cfg->avt.rtp_bw.max) {
		sdp_media_set_lbandwidth(stream_sdpmedia(a->strm),
					 SDP_BANDWIDTH_AS,
					 AUDIO_BANDWIDTH / 1000);
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

		a->extmap_aulevel = stream_generate_extmap_id(a->strm);

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

	if (acc && acc->autelev_pt)
		a->cfg.telev_pt = acc->autelev_pt;

	err = telev_alloc(&a->telev, ptime);
	if (err)
		goto out;

	err = add_telev_codec(a);
	if (err)
		goto out;

	if (acc && acc->ausrc_mod) {

		tx->module = mem_ref(acc->ausrc_mod);
		tx->device = mem_ref(acc->ausrc_dev);

		info("audio: using account specific source: (%s,%s)\n",
		     tx->module, tx->device);
	}
	else {
		err  = str_dup(&tx->module, a->cfg.src_mod);
		err |= str_dup(&tx->device, a->cfg.src_dev);
		if (err)
			goto out;
	}

	tx->ptime  = ptime;
	tx->ts_ext = tx->ts_base = rand_u16();
	tx->marker = true;

	if (acc && acc->auplay_mod) {

		rx->module = mem_ref(acc->auplay_mod);
		rx->device = mem_ref(acc->auplay_dev);

		info("audio: using account specific player: (%s,%s)\n",
		     rx->module, rx->device);
	}
	else {
		err  = str_dup(&rx->module, a->cfg.play_mod);
		err |= str_dup(&rx->device, a->cfg.play_dev);
		if (err)
			goto out;
	}

	rx->pt     = -1;
	rx->ptime  = ptime;

	err  = mutex_alloc(&tx->mtx);
	err |= mutex_alloc(&rx->mtx);
	if (err)
		goto out;

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


static int tx_thread(void *arg)
{
	struct audio *a = arg;
	struct autx *tx = &a->tx;
	uint64_t ts = 0;

	mtx_lock(tx->mtx);
	while (re_atomic_rlx(&tx->thr.run)) {
		uint64_t now;

		mtx_unlock(tx->mtx);
		sys_msleep(4);
		mtx_lock(tx->mtx);

		if (!tx->aubuf_started) {
			mtx_unlock(tx->mtx);
			goto loop;
		}

		if (!re_atomic_rlx(&tx->thr.run))
			break;

		mtx_unlock(tx->mtx);

		now = tmr_jiffies();
		if (!ts)
			ts = now;

		if (ts > now)
			goto loop;

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

		/* Exact timing: send Telephony-Events from here.
		 * Be aware check_telev sets tx->mtx, so it must released!
		 */
		check_telev(a, tx);

loop:
		mtx_lock(tx->mtx);
	}

	mtx_unlock(tx->mtx);
	return 0;
}


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
			 autx->as ? autx->as->name : "(src)");

	err |= re_hprintf(pf, " ---> aubuf");
	for (le = list_head(&autx->filtl); le; le = le->next) {
		struct aufilt_enc_st *st = le->data;

		if (st->af->ench)
			err |= re_hprintf(pf, " ---> %s", st->af->name);
	}

	err |= re_hprintf(pf, " ---> %s\n",
			  autx->ac ? autx->ac->name : "(encoder)");

	return err;
}


static int aurx_print_pipeline(struct re_printf *pf, const struct aurx *rx)
{
	struct le *le;
	int err;

	if (!rx)
		return 0;

	err = re_hprintf(pf, "audio rx pipeline:  %10s",
			 rx->ap ? rx->ap->name : "(play)");

	err |= re_hprintf(pf, " <--- aubuf");
	mtx_lock(rx->mtx);
	for (le = list_head(&rx->filtl); le; le = le->next) {
		struct aufilt_dec_st *st = le->data;

		if (st->af->dech)
			err |= re_hprintf(pf, " <--- %s", st->af->name);
	}

	mtx_unlock(rx->mtx);
	err |= re_hprintf(pf, " <--- %s\n",
			  rx->ac ? rx->ac->name : "(decoder)");

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
	struct aufilt_prm encprm, plprm;
	struct autx *tx = &a->tx;
	struct aurx *rx = &a->rx;
	struct le *le;
	bool update_enc = false, update_dec = false;
	int err = 0;

	/* wait until we have both Encoder and Decoder */
	if (!tx->ac || !rx->ac)
		return 0;

	if (list_isempty(&tx->filtl))
		update_enc = true;

	mtx_lock(rx->mtx);
	if (list_isempty(&rx->filtl))
		update_dec = true;

	mtx_unlock(rx->mtx);
	aufilt_param_set(&encprm, tx->ac, tx->enc_fmt);
	aufilt_param_set(&plprm, rx->ac, rx->dec_fmt);
	if (a->cfg.srate_play && a->cfg.srate_play != plprm.srate) {
		plprm.srate = a->cfg.srate_play;
	}

	if (a->cfg.channels_play && a->cfg.channels_play != plprm.ch) {
		plprm.ch = a->cfg.channels_play;
	}

	/* Audio filters */
	for (le = list_head(aufiltl); le; le = le->next) {
		struct aufilt *af = le->data;
		struct aufilt_enc_st *encst = NULL;
		struct aufilt_dec_st *decst = NULL;
		void *ctx = NULL;

		if (af->encupdh && update_enc) {
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

		if (af->decupdh && update_dec) {
			err = af->decupdh(&decst, &ctx, af, &plprm, a);
			if (err) {
				warning("audio: error in decode audio-filter"
					" '%s' (%m)\n", af->name, err);
			}
			else {
				decst->af = af;
				mtx_lock(rx->mtx);
				list_append(&rx->filtl, &decst->le, decst);
				mtx_unlock(rx->mtx);
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
	size_t sz;
	size_t min_sz;
	size_t max_sz;
	int err = 0;

	if (!ac)
		return 0;

	srate_dsp    = ac->srate;
	channels_dsp = ac->ch;

	if (a->cfg.srate_play && a->cfg.srate_play != srate_dsp) {
		srate_dsp = a->cfg.srate_play;
	}
	if (a->cfg.channels_play && a->cfg.channels_play != channels_dsp) {
		channels_dsp = a->cfg.channels_play;
	}

	/* Start Audio Player */
	if (!rx->auplay && auplay_find(auplayl, NULL)) {

		struct auplay_prm prm;

		prm.srate      = srate_dsp;
		prm.ch         = channels_dsp;
		prm.ptime      = rx->ptime;
		prm.fmt        = rx->play_fmt;

		if (!rx->aubuf) {
			const uint16_t ptime_min = (uint16_t)a->cfg.buffer.min;
			const uint16_t ptime_max = (uint16_t)a->cfg.buffer.max;
			sz = aufmt_sample_size(rx->play_fmt);

			if (!ptime_min || !ptime_max)
				return EINVAL;

			min_sz = sz*calc_nsamp(prm.srate, prm.ch, ptime_min);
			max_sz = sz*calc_nsamp(prm.srate, prm.ch, ptime_max);

			debug("audio: create auplay buffer"
			      " [%u - %u ms]"
			      " [%zu - %zu bytes]\n",
			      ptime_min, ptime_max, min_sz, max_sz);

			err = aubuf_alloc(&rx->aubuf, min_sz, max_sz);
			if (err) {
				warning("audio: aubuf alloc error (%m)\n",
					err);
				return err;
			}

			aubuf_set_mode(rx->aubuf, a->cfg.adaptive ?
				       AUBUF_ADAPTIVE : AUBUF_FIXED);
			aubuf_set_silence(rx->aubuf, a->cfg.silence);
			rx->aubuf_minsz = min_sz;
			rx->aubuf_maxsz = max_sz;
		}

		rx->auplay_prm = prm;
		err = auplay_alloc(&rx->auplay, auplayl,
				   rx->module,
				   &prm, rx->device,
				   auplay_write_handler, a);
		if (err) {
			warning("audio: start_player failed (%s.%s): %m\n",
				rx->module, rx->device, err);
			goto out;
		}

		rx->ap = auplay_find(auplayl, rx->module);

		info("audio: player started with sample format %s\n",
		     aufmt_name(rx->play_fmt));

	}

out:
	if (err)
		rx->aubuf    = mem_deref(rx->aubuf);

	return 0;
}


static int start_source(struct autx *tx, struct audio *a, struct list *ausrcl)
{
	const struct aucodec *ac = tx->ac;
	uint32_t srate_dsp;
	uint32_t channels_dsp;
	int err;

	if (!ac)
		return 0;

	srate_dsp    = ac->srate;
	channels_dsp = ac->ch;

	if (a->cfg.srate_src && a->cfg.srate_src != srate_dsp) {
		srate_dsp = a->cfg.srate_src;
	}
	if (a->cfg.channels_src && a->cfg.channels_src != channels_dsp) {
		channels_dsp = a->cfg.channels_src;
	}

	/* Start Audio Source */
	if (!tx->ausrc && ausrc_find(ausrcl, NULL) && !a->hold) {

		struct ausrc_prm prm;
		size_t sz;
		size_t psize_alloc;

		prm.srate      = srate_dsp;
		prm.ch         = channels_dsp;
		prm.ptime      = tx->ptime;
		prm.fmt        = tx->src_fmt;

		tx->ausrc_prm = prm;

		sz = aufmt_sample_size(tx->src_fmt);

		psize_alloc = sz * calc_nsamp(prm.srate, prm.ch, prm.ptime);
		tx->psize = psize_alloc;
		tx->aubuf_maxsz = tx->psize * 30;

		if (!tx->aubuf) {
			err = aubuf_alloc(&tx->aubuf, tx->psize,
					  tx->aubuf_maxsz);
			if (err)
				return err;
		}

		err = ausrc_alloc(&tx->ausrc, ausrcl,
				  tx->module,
				  &prm, tx->device,
				  ausrc_read_handler, ausrc_error_handler, a);
		if (err) {
			warning("audio: start_source failed (%s.%s): %m\n",
				tx->module, tx->device, err);
			return err;
		}

		mtx_lock(tx->mtx);
		/* recalculate and resize aubuf if ausrc_alloc changes prm */
		tx->src_fmt = prm.fmt;
		sz = aufmt_sample_size(tx->src_fmt);
		tx->psize = sz * calc_nsamp(prm.srate, prm.ch, prm.ptime);
		if (psize_alloc != tx->psize) {
			tx->ausrc_prm = prm;
			tx->aubuf_maxsz = tx->psize * 30;
			err = aubuf_resize(tx->aubuf, tx->psize,
					   tx->aubuf_maxsz);
			if (err) {
				mtx_unlock(tx->mtx);
				return err;
			}
		}

		mtx_unlock(tx->mtx);
		tx->as = ausrc_find(ausrcl, tx->module);

		switch (a->cfg.txmode) {

		case AUDIO_MODE_POLL:
			break;

		case AUDIO_MODE_THREAD:
			if (!re_atomic_rlx(&tx->thr.run)) {
				re_atomic_rlx_set(&tx->thr.run, true);
				err = thread_create_name(&tx->thr.tid,
							 "Audio Source",
							 tx_thread, a);
				if (err) {
					re_atomic_rlx_set(&tx->thr.run,
							   false);
					return err;
				}
			}
			break;

		default:
			warning("audio: tx mode not supported (%d)\n",
				a->cfg.txmode);
			return ENOTSUP;
		}

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
	a->started = false;
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
			mtx_lock(a->tx.mtx);
			list_flush(&tx->filtl);
			mtx_unlock(a->tx.mtx);
			aubuf_flush(tx->aubuf);
		}

		tx->enc = mem_deref(tx->enc);
		tx->ac = ac;
	}

	if (ac->encupdh) {
		struct auenc_param prm;

		prm.bitrate = 0;        /* auto */

		err = ac->encupdh(&tx->enc, ac, &prm, params);
		if (err) {
			warning("audio: alloc encoder: %m\n", err);
			return err;
		}
	}

	stream_set_srate(a->strm, ac->crate, 0);

	mtx_lock(a->tx.mtx);
	stream_update_encoder(a->strm, pt_tx);
	mtx_unlock(a->tx.mtx);

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

	if (reset || ac != rx->ac) {
		rx->auplay = mem_deref(rx->auplay);
		aubuf_flush(rx->aubuf);
		stream_flush(a->strm);

		/* Reset audio filter chain */
		mtx_lock(rx->mtx);
		list_flush(&rx->filtl);
		mtx_unlock(rx->mtx);
	}

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

	if (!rx->auplay)
		err |= audio_start(a);

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

		mtx_lock(a->tx.mtx);
		err = telev_send(a->telev, event, false);
		mtx_unlock(a->tx.mtx);

	}
	else if (a->tx.cur_key && a->tx.cur_key != KEYCODE_REL) {
		/* Key release */
		info("audio: send DTMF digit end: '%c'\n", a->tx.cur_key);
		mtx_lock(a->tx.mtx);
		err = telev_send(a->telev,
				 telev_digit2code(a->tx.cur_key), true);
		mtx_unlock(a->tx.mtx);
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
			  tx->as ? tx->as->name : "none",
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
			  rx->ap ? rx->ap->name : "none",
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
				  mod, &tx->ausrc_prm, device,
				  ausrc_read_handler, ausrc_error_handler, au);
		if (err) {
			warning("audio: set_source failed (%s.%s): %m\n",
				mod, device, err);
			return err;
		}

		tx->as = ausrc_find(baresip_ausrcl(), mod);
	}

	return 0;
}


/**
 * Set the audio player state to a new audio player module and device.
 * The current audio player will be stopped.
 *
 * @param a      Audio object
 * @param mod    Audio player module (NULL to stop)
 * @param device Audio player device name
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_set_player(struct audio *a, const char *mod, const char *device)
{
	struct aurx *rx;
	int err;

	if (!a)
		return EINVAL;

	rx = &a->rx;

	/* stop the audio device first */
	rx->auplay = mem_deref(rx->auplay);

	if (str_isset(mod)) {

		err = auplay_alloc(&rx->auplay, baresip_auplayl(),
				   mod, &rx->auplay_prm, device,
				   auplay_write_handler, a);
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
 * Set the audio stream on conference
 *
 * @param au          Audio object
 * @param conference  True for conference, false for not
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_set_conference(struct audio *au, bool conference)
{
	if (!au)
		return EINVAL;

	au->conference = conference;

	return 0;
}


/**
 * Is audio on conference?
 *
 * @param au    Audio object
 *
 * @return true if on conference, false if not
 */
bool audio_is_conference(const struct audio *au)
{
	return au ? au->conference : false;
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
