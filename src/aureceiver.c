/**
 * @file src/aureceiver.c  Audio stream receiver
 *
 * Copyright (C) 2023 Alfred E. Heggestad, Christian Spielberger
 */
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <re_atomic.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


/**
 * Audio receive pipeline
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

enum {
	JITTER_EMA_COEFF   = 128,     /**< Jitter EMA coefficient            */
};


struct audio_recv {
	uint32_t srate;               /**< Decoder sample rate               */
	uint32_t ch;                  /**< Decoder channel number            */
	enum aufmt fmt;               /**< Decoder sample format             */
	const struct config_audio *cfg;  /**< Audio configuration            */
	struct audec_state *dec;      /**< Audio decoder state (optional)    */
	const struct aucodec *ac;     /**< Current audio decoder             */
	struct aubuf *aubuf;          /**< Audio buffer before auplay        */
	mtx_t *aubuf_mtx;             /**< Mutex for aubuf allocation        */
	uint32_t ssrc;                /**< Incoming synchronization source   */
	struct list filtl;            /**< Audio filters in decoding order   */
	void *sampv;                  /**< Sample buffer                     */
	size_t sampvsz;               /**< Sample buffer size                */
	uint64_t t;                   /**< Last auframe push time            */
	uint32_t ptime;               /**< Packet time for receiving [us]    */

	double level_last;            /**< Last audio level value [dBov]     */
	bool level_set;               /**< True if level_last is set         */
	struct timestamp_recv ts_recv;/**< Receive timestamp state           */
	uint8_t extmap_aulevel;       /**< ID Range 1-14 inclusive           */
	int pt;                       /**< Payload type of audio codec       */

	struct {
		uint64_t n_discard;   /**< Nbr of discarded packets          */
		RE_ATOMIC uint64_t latency;   /**< Latency in [ms]           */
		int32_t jitter;       /**< Auframe push jitter [us]          */
		int32_t dmax;         /**< Max deviation [us]                */
	} stats;

	mtx_t *mtx;

	const struct auplay *ap;      /**< Audio player module               */
	struct auplay_st *auplay;     /**< Audio player                      */
	struct auplay_prm auplay_prm; /**< Audio player parameters           */
	char *module;                 /**< Audio player module name          */
	char *device;                 /**< Audio player device name          */
	enum aufmt play_fmt;          /**< Sample format for audio playback  */
	bool done_first;              /**< First auplay write done flag      */
};


static void destructor(void *arg)
{
	struct audio_recv *ar = arg;

	mem_deref(ar->dec);
	mem_deref(ar->aubuf);
	mem_deref(ar->aubuf_mtx);
	mem_deref(ar->sampv);
	mem_deref(ar->mtx);
	list_flush(&ar->filtl);
	mem_deref(ar->module);
	mem_deref(ar->device);
}


static int aurecv_process_decfilt(struct audio_recv *ar, struct auframe *af)
{
	int err = 0;

	/* Process exactly one audio-frame in reverse list order */
	for (struct le *le = ar->filtl.tail; le; le = le->prev) {
		struct aufilt_dec_st *st = le->data;

		if (st->af && st->af->dech)
			err = st->af->dech(st, af);

		if (err)
			break;
	}

	return err;
}


static double aurecv_calc_seconds(const struct audio_recv *ar)
{
	uint64_t dur;
	double seconds;

	if (!ar->ac)
		return .0;

	dur = timestamp_duration(&ar->ts_recv);
	seconds = timestamp_calc_seconds(dur, ar->ac->crate);

	return seconds;
}


static int aurecv_alloc_aubuf(struct audio_recv *ar, const struct auframe *af)
{
	size_t min_sz;
	size_t max_sz;
	size_t sz;
	const struct config_audio *cfg = ar->cfg;
	int err;

	sz = aufmt_sample_size(cfg->play_fmt);
	min_sz = sz * au_calc_nsamp(af->srate, af->ch, cfg->buffer.min);
	max_sz = sz * au_calc_nsamp(af->srate, af->ch, cfg->buffer.max);

	debug("audio_recv: create audio buffer"
	      " [%u - %u ms]"
	      " [%zu - %zu bytes]\n",
	      (unsigned) cfg->buffer.min, (unsigned) cfg->buffer.max,
	      min_sz, max_sz);

	mtx_lock(ar->aubuf_mtx);
	err = aubuf_alloc(&ar->aubuf, min_sz, max_sz);
	if (err) {
		warning("audio_recv: aubuf alloc error (%m)\n", err);
		goto out;
	}

	struct pl *id = pl_alloc_str("aureceiver");
	if (!id) {
		ar->aubuf = mem_deref(ar->aubuf);
		err = ENOMEM;
		goto out;
	}

	aubuf_set_id(ar->aubuf, id);
	mem_deref(id);

	aubuf_set_mode(ar->aubuf, cfg->adaptive ?
		       AUBUF_ADAPTIVE : AUBUF_FIXED);
	aubuf_set_silence(ar->aubuf, cfg->silence);

out:
	mtx_unlock(ar->aubuf_mtx);

	return err;
}


static int aurecv_push_aubuf(struct audio_recv *ar, const struct auframe *af)
{
	int err;
	uint64_t bpms;

	if (!ar->aubuf) {
		err = aurecv_alloc_aubuf(ar, af);
		if (err)
			return err;
	}

#ifndef RELEASE
	int32_t d, da;
	uint64_t t;
	t = tmr_jiffies_usec();
	if (ar->t) {
		d = (int32_t) (int64_t) ((t - ar->t) - ar->ptime);
		da = abs(d);
		ar->stats.dmax = max(ar->stats.dmax, da);
		ar->stats.jitter += (da - ar->stats.jitter) / JITTER_EMA_COEFF;
	}

	ar->t = t;
#endif
	err = aubuf_write_auframe(ar->aubuf, af);
	if (err)
		return err;

	ar->srate = af->srate;
	ar->ch    = af->ch;
	ar->fmt   = af->fmt;

	bpms = (uint64_t)ar->srate * ar->ch * aufmt_sample_size(ar->fmt) /
	       1000;
	if (bpms)
		re_atomic_rlx_set(&ar->stats.latency,
				  aubuf_cur_size(ar->aubuf) / bpms);

	return 0;
}


static int aurecv_stream_decode(struct audio_recv *ar,
				const struct rtp_header *hdr,
				struct mbuf *mb, unsigned lostc, bool drop)
{
	struct auframe af;
	size_t sampc = ar->sampvsz / aufmt_sample_size(ar->fmt);
	bool marker = hdr->m;
	int err = 0;
	const struct aucodec *ac = ar->ac;
	bool flush = ar->ssrc != hdr->ssrc;

	/* No decoder set */
	if (!ac)
		return 0;

	ar->ssrc = hdr->ssrc;

	/* TODO: PLC */
	if (lostc && ac->plch) {

		err = ac->plch(ar->dec,
				   ar->fmt, ar->sampv, &sampc,
				   mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio_recv: %s codec decode %zu bytes: %m\n",
				ac->name, mbuf_get_left(mb), err);
			goto out;
		}
	}
	else if (mbuf_get_left(mb)) {

		err = ac->dech(ar->dec,
				   ar->fmt, ar->sampv, &sampc,
				   marker, mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio_recv: %s codec decode %zu bytes: %m\n",
				ac->name, mbuf_get_left(mb), err);
			goto out;
		}
	}
	else {
		/* no PLC in the codec, might be done in filters below */
		sampc = 0;
	}

	auframe_init(&af, ar->fmt, ar->sampv, sampc, ac->srate, ac->ch);
	af.timestamp = ((uint64_t) hdr->ts) * AUDIO_TIMEBASE / ac->crate;

	if (drop) {
		aubuf_drop_auframe(ar->aubuf, &af);
		goto out;
	}

	if (flush)
		aubuf_flush(ar->aubuf);

	err = aurecv_process_decfilt(ar, &af);
	if (err)
		goto out;

	err = aurecv_push_aubuf(ar, &af);
 out:
	return err;
}


/* Handle incoming stream data from the network */
void aurecv_receive(struct audio_recv *ar, const struct rtp_header *hdr,
		    struct rtpext *extv, size_t extc,
		    struct mbuf *mb, unsigned lostc, bool *ignore)
{
	bool discard = false;
	bool drop = *ignore;
	int wrap;
	(void) lostc;

	if (!mb)
		return;

	mtx_lock(ar->mtx);
	if (hdr->pt != ar->pt) {
		mtx_unlock(ar->mtx);
		*ignore = true;
		return;
	}

	*ignore = false;

	/* RFC 5285 -- A General Mechanism for RTP Header Extensions */
	const struct rtpext *ext = rtpext_find(extv, extc, ar->extmap_aulevel);
	if (ext) {
		ar->level_last = -(double)(ext->data[0] & 0x7f);
		ar->level_set = true;
	}

	/* Save timestamp for incoming RTP packets */

	if (!ar->ts_recv.is_set)
		timestamp_set(&ar->ts_recv, hdr->ts);

	wrap = timestamp_wrap(hdr->ts, ar->ts_recv.last);

	switch (wrap) {

	case -1:
		warning("audio_recv: rtp timestamp wraps backwards"
			" (delta = %d) -- discard\n",
			(int32_t)(ar->ts_recv.last - hdr->ts));
		discard = true;
		break;

	case 0:
		break;

	case 1:
		++ar->ts_recv.num_wraps;
		break;

	default:
		break;
	}

	ar->ts_recv.last = hdr->ts;

	if (discard) {
		++ar->stats.n_discard;
		goto out;
	}

	/* TODO:  what if lostc > 1 ?*/
	/* PLC should generate lostc frames here. Not only one.
	 * aubuf should replace PLC frames with late arriving real frames.
	 * It should use timestamp to decide if a frame should be replaced. */
/*        if (lostc)*/
/*                (void)aurecv_stream_decode(ar, hdr, mb, lostc, drop);*/

	(void)aurecv_stream_decode(ar, hdr, mb, 0, drop);

out:
	mtx_unlock(ar->mtx);
}


void aurecv_set_extmap(struct audio_recv *ar, uint8_t aulevel)
{
	if (!ar)
		return;

	mtx_lock(ar->mtx);
	ar->extmap_aulevel = aulevel;
	mtx_unlock(ar->mtx);
}


int aurecv_set_module(struct audio_recv *ar, const char *module)
{
	if (!ar)
		return EINVAL;

	ar->module = mem_deref(ar->module);
	return str_dup(&ar->module, module);
}


int aurecv_set_device(struct audio_recv *ar, const char *device)
{
	if (!ar)
		return EINVAL;

	ar->device = mem_deref(ar->device);
	return str_dup(&ar->device, device);
}


uint64_t aurecv_latency(const struct audio_recv *ar)
{
	if (!ar)
		return 0;

	return re_atomic_rlx(&ar->stats.latency);
}


int aurecv_alloc(struct audio_recv **aupp, const struct config_audio *cfg,
		 size_t sampc, uint32_t ptime)
{
	struct audio_recv *ar;
	int err;

	if (!aupp)
		return EINVAL;

	ar = mem_zalloc(sizeof(*ar), destructor);
	if (!ar)
		return ENOMEM;

	ar->cfg = cfg;
	ar->srate = cfg->srate_play;
	ar->ch    = cfg->channels_play;
	ar->fmt   = cfg->dec_fmt;
	ar->play_fmt = cfg->play_fmt;
	ar->sampvsz = sampc * aufmt_sample_size(ar->fmt);
	ar->sampv   = mem_zalloc(ar->sampvsz, NULL);
	ar->ptime   = ptime * 1000;
	ar->pt      = -1;
	if (!ar->sampv) {
		err = ENOMEM;
		goto out;
	}

	err  = mutex_alloc(&ar->mtx);
	err |= mutex_alloc(&ar->aubuf_mtx);

out:
	if (err)
		mem_deref(ar);
	else
		*aupp = ar;

	return err;
}


void aurecv_flush(struct audio_recv *ar)
{
	if (!ar)
		return;

	mtx_lock(ar->mtx);
	aubuf_flush(ar->aubuf);

	/* Reset audio filter chain */
	list_flush(&ar->filtl);
	mtx_unlock(ar->mtx);
}


int aurecv_decoder_set(struct audio_recv *ar,
		       const struct aucodec *ac, int pt, const char *params)
{
	int err = 0;

	if (!ar || !ac)
		return EINVAL;

	info("audio_recv: Set audio decoder: %s %uHz %dch\n",
	     ac->name, ac->srate, ac->ch);

	mtx_lock(ar->mtx);
	if (ac != ar->ac) {
		ar->ac = ac;
		ar->dec = mem_deref(ar->dec);
	}

	if (ac->decupdh) {
		err = ac->decupdh(&ar->dec, ac, params);
		if (err) {
			warning("audio_recv: alloc decoder: %m\n", err);
			goto out;
		}
	}

	ar->pt = pt;

out:
	mtx_unlock(ar->mtx);
	return err;
}


int aurecv_payload_type(const struct audio_recv *ar)
{
	if (!ar)
		return -1;

	return ar->pt;
}


int aurecv_filt_append(struct audio_recv *ar, struct aufilt_dec_st *decst)
{
	if (!ar || !decst)
		return EINVAL;

	mtx_lock(ar->mtx);
	list_append(&ar->filtl, &decst->le, decst);
	mtx_unlock(ar->mtx);

	return 0;
}


bool aurecv_filt_empty(const struct audio_recv *ar)
{
	bool empty;
	if (!ar)
		return false;

	mtx_lock(ar->mtx);
	empty = list_isempty(&ar->filtl);
	mtx_unlock(ar->mtx);

	return empty;
}


bool aurecv_level_set(const struct audio_recv *ar)
{
	bool set;
	if (!ar)
		return false;

	mtx_lock(ar->mtx);
	set = ar->level_set;
	mtx_unlock(ar->mtx);

	return set;
}


double aurecv_level(const struct audio_recv *ar)
{
	double v;
	if (!ar)
		return 0.0;

	mtx_lock(ar->mtx);
	v = ar->level_last;
	mtx_unlock(ar->mtx);

	return v;
}


const struct aucodec *aurecv_codec(const struct audio_recv *ar)
{
	const struct aucodec *ac;

	if (!ar)
		return NULL;

	mtx_lock(ar->mtx);
	ac = ar->ac;
	mtx_unlock(ar->mtx);
	return ac;
}


static void aurecv_read(struct audio_recv *ar, struct auframe *af)
{
	if (!ar || mtx_trylock(ar->aubuf_mtx) != thrd_success)
		return;

	if (ar->aubuf)
		aubuf_read_auframe(ar->aubuf, af);
	else
		memset(af->sampv, 0, auframe_size(af));

	mtx_unlock(ar->aubuf_mtx);
}


void aurecv_stop(struct audio_recv *ar)
{
	if (!ar)
		return;

	ar->auplay = mem_deref(ar->auplay);
	mtx_lock(ar->mtx);
	ar->ac = NULL;
	mtx_unlock(ar->mtx);
}


void aurecv_stop_auplay(struct audio_recv *ar)
{
	if (!ar)
		return;

	ar->auplay = mem_deref(ar->auplay);
}


static void check_plframe(struct auframe *af1, struct auframe *af2)
{
	if ((af1->srate && af1->srate != af2->srate) ||
	    (af1->ch    && af1->ch    != af2->ch   )) {
		warning("audio_recv: srate/ch of frame %u/%u vs "
			"player %u/%u. Use module auresamp!\n",
			af1->srate, af1->ch,
			af2->srate, af2->ch);
	}

	if (af1->fmt != af2->fmt) {
		warning("audio_recv: invalid sample formats (%s -> %s). "
			"%s\n",
			aufmt_name(af1->fmt), aufmt_name(af2->fmt),
			af1->fmt == AUFMT_S16LE ?
			"Use module auconv!" : "");
	}
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
 * @note The sample format is set in ar->play_fmt
 */
static void auplay_write_handler(struct auframe *af, void *arg)
{
	struct audio_recv *ar = arg;

	if (!ar->done_first) {
		struct auframe afr;
		memset(&afr, 0, sizeof(afr));
		afr = *af;

		aurecv_read(ar, af);

		check_plframe(&afr, af);
		ar->done_first = true;
		return;
	}

	aurecv_read(ar, af);
}


int aurecv_start_player(struct audio_recv *ar, struct list *auplayl)
{
	const struct aucodec *ac = aurecv_codec(ar);
	uint32_t srate_dsp;
	uint32_t channels_dsp;
	int err = 0;

	if (!ac)
		return 0;

	srate_dsp    = ac->srate;
	channels_dsp = ac->ch;

	if (ar->cfg->srate_play && ar->cfg->srate_play != srate_dsp) {
		srate_dsp = ar->cfg->srate_play;
	}
	if (ar->cfg->channels_play && ar->cfg->channels_play != channels_dsp) {
		channels_dsp = ar->cfg->channels_play;
	}

	/* Start Audio Player */
	if (!ar->auplay && auplay_find(auplayl, NULL)) {

		struct auplay_prm prm;

		prm.srate      = srate_dsp;
		prm.ch         = channels_dsp;
		prm.ptime      = ar->ptime / 1000;
		prm.fmt        = ar->play_fmt;

		ar->auplay_prm = prm;
		err = auplay_alloc(&ar->auplay, auplayl,
				   ar->module,
				   &prm, ar->device,
				   auplay_write_handler, ar);
		if (err) {
			warning("audio_recv: start_player failed (%s.%s): "
				"%m\n",
				ar->module, ar->device, err);
			goto out;
		}

		ar->ap = auplay_find(auplayl, ar->module);

		info("audio_recv: player started with sample format %s\n",
		     aufmt_name(ar->play_fmt));
	}

out:

	return 0;
}


bool aurecv_started(const struct audio_recv *ar)
{
	bool ret;

	if (!ar || mtx_trylock(ar->aubuf_mtx) != thrd_success)
		return false;

	ret = aubuf_started(ar->aubuf);
	mtx_unlock(ar->aubuf_mtx);
	return ret;
}


bool aurecv_player_started(const struct audio_recv *ar)
{
	return ar ? ar->auplay != NULL : false;
}


int aurecv_debug(struct re_printf *pf, const struct audio_recv *ar)
{
	struct mbuf *mb;
	double bpms;
	int err;

	if (!ar)
		return 0;

	mb = mbuf_alloc(32);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	mtx_lock(ar->mtx);
	bpms = (double)ar->srate * ar->ch * aufmt_sample_size(ar->fmt) /
	       1000.0;
	err  = mbuf_printf(mb,
			   " rx:   decode: %H %s\n",
			   aucodec_print, ar->ac,
			   aufmt_name(ar->fmt));
	mtx_lock(ar->aubuf_mtx);
	err |= mbuf_printf(mb, "       aubuf: %H"
			   " (cur %.2fms, max %.2fms)\n",
			   aubuf_debug, ar->aubuf,
			   aubuf_cur_size(ar->aubuf) / bpms,
			   aubuf_maxsz(ar->aubuf) / bpms);
	mtx_unlock(ar->aubuf_mtx);
#ifndef RELEASE
	err |= mbuf_printf(mb, "       SW jitter: %.2fms\n",
			   (double) ar->stats.jitter / 1000);
	err |= mbuf_printf(mb, "       deviation: %.2fms\n",
			   (double) ar->stats.dmax / 1000);
#endif
	err |= mbuf_printf(mb, "       n_discard: %llu\n",
			   ar->stats.n_discard);
	if (ar->level_set) {
		err |= mbuf_printf(mb, "       level %.3f dBov\n",
				   ar->level_last);
	}
	if (ar->ts_recv.is_set) {
		err |= mbuf_printf(mb, "       time = %.3f sec\n",
				   aurecv_calc_seconds(ar));
	}
	else {
		err |= mbuf_printf(mb, "       time = (not started)\n");
	}

	err |= mbuf_printf(mb, "       player: %s,%s %s\n",
			  ar->ap ? ar->ap->name : "none",
			  ar->device,
			  aufmt_name(ar->play_fmt));
	mtx_unlock(ar->mtx);

	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);
out:
	mem_deref(mb);
	return err;
}


int aurecv_print_pipeline(struct re_printf *pf, const struct audio_recv *ar)
{
	struct mbuf *mb;
	struct le *le;
	int err;

	if (!ar)
		return 0;

	mb = mbuf_alloc(32);
	if (!mb)
		return ENOMEM;

	err = re_hprintf(pf, "audio rx pipeline:  %10s",
			 ar->ap ? ar->ap->name : "(play)");
	err |= mbuf_printf(mb, " <--- aubuf");
	mtx_lock(ar->mtx);
	for (le = list_head(&ar->filtl); le; le = le->next) {
		struct aufilt_dec_st *st = le->data;

		if (st->af->dech)
			err |= mbuf_printf(mb, " <--- %s", st->af->name);
	}
	mtx_unlock(ar->mtx);

	err |= mbuf_printf(mb, " <--- %s",
			   ar->ac ? ar->ac->name : "(decoder)");

	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);
out:
	mem_deref(mb);
	return err;
}
