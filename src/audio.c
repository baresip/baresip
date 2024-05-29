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


struct audio_recv;


/** Generic Audio stream */
struct audio {
	MAGIC_DECL                    /**< Magic number for debugging      */
	struct autx tx;               /**< Transmit                        */
	struct audio_recv *aur;       /**< Audio Receiver                  */
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
	if (!au)
		return 0;

	return aurecv_latency(au->aur);
}


static double autx_calc_seconds(const struct autx *autx)
{
	uint64_t dur;

	if (!autx->ac)
		return .0;

	mtx_lock(autx->mtx);
	dur = autx->ts_ext - autx->ts_base;
	mtx_unlock(autx->mtx);

	return timestamp_calc_seconds(dur, autx->ac->crate);
}


static void stop_tx(struct autx *tx, struct audio *a)
{
	if (!tx || !a)
		return;

	stream_enable_tx(a->strm, false);
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


static void audio_destructor(void *arg)
{
	struct audio *a = arg;

	debug("audio: destroyed (started=%d)\n", a->started);

	stop_tx(&a->tx, a);
	stream_enable_rx(a->strm, false);
	aurecv_stop(a->aur);

	mem_deref(a->tx.enc);
	mem_deref(a->tx.aubuf);
	mem_deref(a->tx.mb);
	mem_deref(a->tx.sampv);
	mem_deref(a->tx.module);
	mem_deref(a->tx.device);

	list_flush(&a->tx.filtl);

	mem_deref(a->strm);
	mem_deref(a->telev);
	mem_deref(a->aur);

	mem_deref(a->tx.mtx);
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
			mtx_lock(a->tx.mtx);
			tx->ts_ext += ts_delta;
			mtx_unlock(a->tx.mtx);
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

	mtx_lock(a->tx.mtx);
	tx->ts_ext += (uint32_t)frame_size;
	mtx_unlock(a->tx.mtx);

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
	auframe_init(&af, tx->src_fmt, tx->sampv, sampc, srate, ch);
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
	if (a->errh)
		a->errh(err, str, a->arg);
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


static int stream_pt_handler(uint8_t pt, struct mbuf *mb, void *arg)
{
	struct audio *a = arg;
	const struct sdp_format *lc;

	lc = sdp_media_lformat(stream_sdpmedia(a->strm), pt);

	/* Telephone event? */
	if (lc && !str_casecmp(lc->name, "telephone-event")) {
		handle_telev(a, mb);
		return ENODATA;
	}

	if (!lc)
		return ENOENT;

	int ptc = aurecv_payload_type(a->aur);
	if (ptc == (int) pt)
		return 0;

	if (ptc != -1)
		info("Audio decoder changed payload %d -> %u\n", ptc, pt);

	return audio_decoder_set(a, lc->data, lc->pt, lc->params);
}


/**
 * Stream receive handler for audio is called from RX thread if enabled
 */
static void stream_recv_handler(const struct rtp_header *hdr,
				struct rtpext *extv, size_t extc,
				struct mbuf *mb, unsigned lostc, bool *ignore,
				void *arg)
{
	struct audio *a = arg;

	MAGIC_CHECK(a);

	if (!a->aur)
		return;

	aurecv_receive(a->aur, hdr, extv, extc, mb, lostc, ignore);
}


static int add_telev_codec(struct audio *a)
{
	struct sdp_media *m = stream_sdpmedia(audio_strm(a));
	struct sdp_format *sf;
	uint32_t pt = a->cfg.telev_pt;
	char pts[11];
	bool add = !sdp_media_lformat(m, pt);
	int err;

	(void)re_snprintf(pts, sizeof(pts), "%u", pt);

	/* Use payload-type 101 if available, for CiscoGW interop */
	err = sdp_format_add(&sf, m, false,
			     add ? pts : NULL,
			     telev_rtpfmt, TELEV_SRATE, 1, NULL,
			     NULL, NULL, false, "0-15");
	if (err)
		return err;

	return 0;
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

	tx->src_fmt = cfg->audio.src_fmt;
	tx->enc_fmt = cfg->audio.enc_fmt;

	err = stream_alloc(&a->strm, streaml,
			   stream_prm, &cfg->avt, sdp_sess,
			   MEDIA_AUDIO,
			   mnat, mnat_sess, menc, menc_sess, offerer,
			   stream_recv_handler, NULL, stream_pt_handler,
			   a);
	if (err)
		goto out;

	err = aurecv_alloc(&a->aur, &a->cfg, AUDIO_SAMPSZ, ptime);
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
		aurecv_set_extmap(a->aur, a->extmap_aulevel);

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

	if (!tx->mb || !tx->sampv) {
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
		err  = aurecv_set_module(a->aur, acc->auplay_mod);
		err |= aurecv_set_device(a->aur, acc->auplay_dev);

		info("audio: using account specific player: (%s,%s)\n",
		     acc->auplay_mod, acc->auplay_dev);
	}
	else {
		err  = aurecv_set_module(a->aur, a->cfg.play_mod);
		err |= aurecv_set_device(a->aur, a->cfg.play_dev);
		if (err)
			goto out;
	}

	err  = mutex_alloc(&tx->mtx);
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

	err |= re_hprintf(pf, " ---> %s",
			  autx->ac ? autx->ac->name : "(encoder)");

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
	struct le *le;
	bool update_enc, update_dec;
	int err = 0;

	/* wait until we have both Encoder and Decoder */
	if (!tx->ac || !aurecv_codec(a->aur))
		return 0;

	update_dec = aurecv_filt_empty(a->aur);
	update_enc = list_isempty(&tx->filtl);

	aufilt_param_set(&encprm, tx->ac, tx->enc_fmt);
	aufilt_param_set(&plprm, aurecv_codec(a->aur), a->cfg.play_fmt);
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
				aurecv_filt_append(a->aur, decst);
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
							 "Audio TX",
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

	stream_enable_tx(a->strm, true);

	return 0;
}


static void audio_flush_filters(struct audio *a)
{
	struct autx *tx = &a->tx;

	aurecv_flush(a->aur);

	mtx_lock(a->tx.mtx);
	list_flush(&tx->filtl);
	mtx_unlock(a->tx.mtx);
}


/**
 * Update audio object and start/stop according to media direction
 *
 * @param a Audio object
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_update(struct audio *a)
{
	struct list *aufiltl = baresip_aufiltl();
	struct sdp_media *m;
	enum sdp_dir dir = SDP_INACTIVE;
	const struct sdp_format *sc = NULL;
	int err = 0;

	if (!a)
		return EINVAL;

	debug("audio: update\n");
	m = stream_sdpmedia(audio_strm(a));

	if (!sdp_media_disabled(m)) {
		dir = sdp_media_dir(m);
		sc = sdp_media_rformat(m, NULL);
	}

	if (!sc || !sc->data) {
		info("audio: stream is disabled\n");
		audio_stop(a);
		return 0;
	}

	if (dir & SDP_RECVONLY)
		err |= audio_decoder_set(a, sc->data, sc->pt, sc->rparams);

	if (dir & SDP_SENDONLY)
		err |= audio_encoder_set(a, sc->data, sc->pt, sc->params);

	if (err) {
		warning("audio: start error (%m)\n", err);
		return err;
	}

	/* Audio filter */
	if (!list_isempty(aufiltl)) {

		err = aufilt_setup(a, aufiltl);
		if (err)
			return err;
	}

	if (dir & SDP_RECVONLY) {
		stream_enable_rx(a->strm, true);
	}
	else {
		stream_enable_rx(a->strm, false);
		aurecv_stop(a->aur);
	}

	if (dir & SDP_SENDONLY) {
		err |= start_source(&a->tx, a, baresip_ausrcl());
	}
	else {
		stop_tx(&a->tx, a);
	}

	if (a->tx.ac && aurecv_codec(a->aur)) {

		if (!a->started) {
			info("%H\n%H\n",
			     autx_print_pipeline, &a->tx,
			     aurecv_print_pipeline, a->aur);
		}

		a->started = true;
	}

	return err;
}


/**
 * This function simply calls audio_update() and kept for backward
 * compatibility
 *
 * @param a Audio object
 *
 * @return 0 if success, otherwise errorcode
 *
 * @deprecated Use audio_update() instead
 */
int audio_start(struct audio *a)
{
	return audio_update(a);
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
	stream_enable_rx(a->strm, false);
	aurecv_stop(a->aur);
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
 * @note The audio source has to be started separately
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

	if (!a || !ac)
		return EINVAL;

	tx = &a->tx;

	if (ac != tx->ac) {
		info("audio: Set audio encoder: %s %uHz %dch\n",
		     ac->name, ac->srate, ac->ch);

		/* Should the source be stopped first? */
		if (!aucodec_equal(ac, tx->ac)) {
			tx->ausrc = mem_deref(tx->ausrc);
			audio_flush_filters(a);
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
	if (ac->ptime)
		tx->ptime = ac->ptime;

	return err;
}


/**
 * Set the audio decoder used
 *
 * @note Starts also the player if not already running
 *
 * @param a      Audio object
 * @param ac     Audio codec to use
 * @param pt     Payload type for receiving
 * @param params Optional decoder parameters
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_decoder_set(struct audio *a, const struct aucodec *ac,
		      int pt, const char *params)
{
	int err = 0;
	bool reset = false;

	if (!a || !ac)
		return EINVAL;

	if (ac != aurecv_codec(a->aur)) {
		struct sdp_media *m;

		m = stream_sdpmedia(audio_strm(a));
		reset  = !aucodec_equal(ac, aurecv_codec(a->aur));
		reset |= !(sdp_media_dir(m) & SDP_RECVONLY);
		if (reset) {
			aurecv_stop(a->aur);
			audio_flush_filters(a);
			stream_flush(a->strm);
		}
	}

	err = aurecv_decoder_set(a->aur, ac, pt, params);
	if (err)
		return err;

	stream_set_srate(a->strm, 0, ac->crate);
	if (reset || !aurecv_player_started(a->aur))
		err |= aurecv_start_player(a->aur, baresip_auplayl());

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
	struct audio *a = arg;
	struct sdp_extmap extmap;
	int err;
	(void)name;

	MAGIC_CHECK(a);

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

		a->extmap_aulevel = extmap.id;
		aurecv_set_extmap(a->aur, a->extmap_aulevel);

		err = sdp_media_set_lattr(stream_sdpmedia(a->strm), true,
					  "extmap",
					  "%u %s",
					  a->extmap_aulevel,
					  uri_aulevel);
		if (err)
			return false;

		a->level_enabled = true;
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
 * @param a      Audio object
 * @param levelp Pointer to where to write audio level value
 *
 * @return 0 if success, otherwise errorcode
 */
int audio_level_get(const struct audio *a, double *levelp)
{
	if (!a)
		return EINVAL;

	if (!a->level_enabled)
		return ENOTSUP;

	if (!aurecv_level_set(a->aur))
		return ENOENT;

	if (levelp)
		*levelp = aurecv_level(a->aur);

	return 0;
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
	size_t sztx;
	int err;

	if (!a)
		return 0;

	tx = &a->tx;
	sztx = aufmt_sample_size(tx->src_fmt);

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

	err |= aurecv_debug(pf, a->aur);
	err |= re_hprintf(pf,
			  " %H\n"
			  " %H\n",
			  autx_print_pipeline, tx,
			  aurecv_print_pipeline, a->aur);

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

	err  = str_dup(&a->tx.device, src);
	err |= aurecv_set_device(a->aur, play);

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
	int err;

	if (!a)
		return EINVAL;

	aurecv_stop_auplay(a->aur);
	if (!str_isset(mod))
		return 0;

	err  = aurecv_set_module(a->aur, mod);
	err |= aurecv_set_device(a->aur, device);
	if (err)
		goto out;

	err = aurecv_start_player(a->aur, baresip_auplayl());
out:
	if (err) {
		warning("audio: set player failed (%s.%s): %m\n",
			mod, device, err);
		return err;
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
	if (!au || !au->aur)
		return false;

	return aurecv_started(au->aur);
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

	return tx ? au->tx.ac : aurecv_codec(au->aur);
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
