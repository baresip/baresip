/**
 * @file src/aureceiver.c  Audio stream receiver
 *
 * Copyright (C) 2023 Alfred E. Heggestad, Christian Spielberger
 */
#include <string.h>
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
struct aurpipe {
	uint32_t srate;               /**< Decoder sample rate               */
	uint32_t ch;                  /**< Decoder channel number            */
	enum aufmt fmt;               /**< Decoder sample format             */
	const struct config_audio *cfg;  /**< Audio configuration            */
	struct audec_state *dec;      /**< Audio decoder state (optional)    */
	const struct aucodec *ac;     /**< Current audio decoder             */
	struct aubuf *aubuf;          /**< Audio buffer before auplay        */
	RE_ATOMIC bool ready;         /**< Audio buffer is ready flag        */
	uint32_t ssrc;                /**< Incoming synchronization source   */
	struct list filtl;            /**< Audio filters in decoding order   */
	void *sampv;                  /**< Sample buffer                     */
	size_t sampvsz;               /**< Sample buffer size                */

	double level_last;            /**< Last audio level value [dBov]     */
	bool level_set;               /**< True if level_last is set         */
	struct timestamp_recv ts_recv;/**< Receive timestamp state           */
	uint8_t extmap_aulevel;       /**< ID Range 1-14 inclusive           */
	uint32_t telev_pt;            /**< Payload type for telephone-events */

	struct {
		uint64_t n_discard;
		RE_ATOMIC uint64_t latency;   /**< Latency in [ms]           */
	} stats;

	mtx_t *mtx;
};


static void destructor(void *arg)
{
	struct aurpipe *rp = arg;

	mem_deref(rp->dec);
	mem_deref(rp->aubuf);
	mem_deref(rp->sampv);
	mem_deref(rp->mtx);
	list_flush(&rp->filtl);
}


static int aup_process_decfilt(struct aurpipe *rp, struct auframe *af)
{
	int err = 0;

	/* Process exactly one audio-frame in reverse list order */
	for (struct le *le = rp->filtl.tail; le; le = le->prev) {
		struct aufilt_dec_st *st = le->data;

		if (st->af && st->af->dech)
			err = st->af->dech(st, af);

		if (err)
			break;
	}

	return err;
}


static double aup_calc_seconds(const struct aurpipe *rp)
{
	uint64_t dur;
	double seconds;

	if (!rp->ac)
		return .0;

	dur = timestamp_duration(&rp->ts_recv);
	seconds = timestamp_calc_seconds(dur, rp->ac->crate);

	return seconds;
}


static int aup_alloc_aubuf(struct aurpipe *rp, const struct auframe *af)
{
	size_t min_sz;
	size_t max_sz;
	size_t sz;
	const struct config_audio *cfg = rp->cfg;
	int err;

	sz = aufmt_sample_size(cfg->play_fmt);
	min_sz = sz * calc_nsamp(af->srate, af->ch, cfg->buffer.min);
	max_sz = sz * calc_nsamp(af->srate, af->ch, cfg->buffer.max);

	debug("aurpipe: create audio buffer"
	      " [%u - %u ms]"
	      " [%zu - %zu bytes]\n",
	      (unsigned) cfg->buffer.min, (unsigned) cfg->buffer.max,
	      min_sz, max_sz);

	err = aubuf_alloc(&rp->aubuf, min_sz, max_sz);
	if (err) {
		warning("aurpipe: aubuf alloc error (%m)\n",
			err);
	}

	aubuf_set_mode(rp->aubuf, cfg->adaptive ?
		       AUBUF_ADAPTIVE : AUBUF_FIXED);
	aubuf_set_silence(rp->aubuf, cfg->silence);
	return err;
}


static int aup_push_aubuf(struct aurpipe *rp, const struct auframe *af)
{
	int err;
	uint64_t bpms;

	if (!re_atomic_rlx(&rp->ready)) {
		err = aup_alloc_aubuf(rp, af);
		if (err)
			return err;

		re_atomic_rlx_set(&rp->ready, true);
	}

	err = aubuf_write_auframe(rp->aubuf, af);
	if (err)
		return err;

	rp->srate = af->srate;
	rp->ch    = af->ch;
	rp->fmt   = af->fmt;

	bpms = rp->srate * rp->ch * aufmt_sample_size(rp->fmt) / 1000;
	if (bpms)
		re_atomic_rlx_set(&rp->stats.latency,
				  aubuf_cur_size(rp->aubuf) / bpms);

	return 0;
}


static int aup_stream_decode(struct aurpipe *rp, const struct rtp_header *hdr,
			    struct mbuf *mb, unsigned lostc, bool drop)
{
	struct auframe af;
	size_t sampc = rp->sampvsz / aufmt_sample_size(rp->fmt);
	bool marker = hdr->m;
	int err = 0;
	const struct aucodec *ac = rp->ac;
	bool flush = rp->ssrc != hdr->ssrc;

	/* No decoder set */
	if (!ac)
		return 0;

	rp->ssrc = hdr->ssrc;

	/* TODO: PLC */
	if (lostc && ac->plch) {

		err = ac->plch(rp->dec,
				   rp->fmt, rp->sampv, &sampc,
				   mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio: %s codec decode %u bytes: %m\n",
				ac->name, mbuf_get_left(mb), err);
			goto out;
		}
	}
	else if (mbuf_get_left(mb)) {

		err = ac->dech(rp->dec,
				   rp->fmt, rp->sampv, &sampc,
				   marker, mbuf_buf(mb), mbuf_get_left(mb));
		if (err) {
			warning("audio: %s codec decode %u bytes: %m\n",
				ac->name, mbuf_get_left(mb), err);
			goto out;
		}
	}
	else {
		/* no PLC in the codec, might be done in filters below */
		sampc = 0;
	}

	auframe_init(&af, rp->fmt, rp->sampv, sampc, ac->srate, ac->ch);
	af.timestamp = ((uint64_t) hdr->ts) * AUDIO_TIMEBASE / ac->crate;

	if (drop) {
		aubuf_drop_auframe(rp->aubuf, &af);
		goto out;
	}

	if (flush)
		aubuf_flush(rp->aubuf);

	err = aup_process_decfilt(rp, &af);
	if (err)
		goto out;

	err = aup_push_aubuf(rp, &af);
 out:
	return err;
}


/* RFC 5285 -- A General Mechanism for RTP Header Extensions */
static const struct rtpext *rtpext_find(const struct rtpext *extv, size_t extc,
					uint8_t id)
{
	for (size_t i=0; i<extc; i++) {
		const struct rtpext *rtpext = &extv[i];

		if (rtpext->id == id)
			return rtpext;
	}

	return NULL;
}


/* Handle incoming stream data from the network */
void aup_receive(struct aurpipe *rp, const struct rtp_header *hdr,
		 struct rtpext *extv, size_t extc,
		 struct mbuf *mb, unsigned lostc, bool *ignore)
{
	bool discard = false;
	bool drop = *ignore;
	int wrap;
	(void) lostc;

	if (!mb)
		goto out;

	mtx_lock(rp->mtx);
	if (hdr->pt == rp->telev_pt) {
		mtx_unlock(rp->mtx);
		*ignore = true;
		return;
	}

	*ignore = false;

	/* RFC 5285 -- A General Mechanism for RTP Header Extensions */
	const struct rtpext *ext = rtpext_find(extv, extc, rp->extmap_aulevel);
	if (ext) {
		rp->level_last = -(double)(ext->data[0] & 0x7f);
		rp->level_set = true;
	}

	/* Save timestamp for incoming RTP packets */

	if (!rp->ts_recv.is_set)
		timestamp_set(&rp->ts_recv, hdr->ts);

	wrap = timestamp_wrap(hdr->ts, rp->ts_recv.last);

	switch (wrap) {

	case -1:
		warning("audio: rtp timestamp wraps backwards"
			" (delta = %d) -- discard\n",
			(int32_t)(rp->ts_recv.last - hdr->ts));
		discard = true;
		break;

	case 0:
		break;

	case 1:
		++rp->ts_recv.num_wraps;
		break;

	default:
		break;
	}

	rp->ts_recv.last = hdr->ts;

	if (discard) {
		++rp->stats.n_discard;
		goto unlock;
	}

 out:
	/* TODO:  what if lostc > 1 ?*/
	/* PLC should generate lostc frames here. Not only one.
	 * aubuf should replace PLC frames with late arriving real frames.
	 * It should use timestamp to decide if a frame should be replaced. */
/*        if (lostc)*/
/*                (void)aup_stream_decode(rp, hdr, mb, lostc, drop);*/

	(void)aup_stream_decode(rp, hdr, mb, 0, drop);

unlock:
	mtx_unlock(rp->mtx);
}


void aup_set_extmap(struct aurpipe *rp, uint8_t aulevel)
{
	if (!rp)
		return;

	mtx_lock(rp->mtx);
	rp->extmap_aulevel = aulevel;
	mtx_unlock(rp->mtx);
}


void aup_set_telev_pt(struct aurpipe *rp, int pt)
{
	if (!rp)
		return;

	mtx_lock(rp->mtx);
	rp->telev_pt = pt;
	mtx_unlock(rp->mtx);
}


uint64_t aup_latency(const struct aurpipe *rp)
{
	if (!rp)
		return 0;

	return re_atomic_rlx(&rp->stats.latency);
}


int aup_alloc(struct aurpipe **aupp, const struct config_audio *cfg,
	      size_t sampc)
{
	struct aurpipe *rp;
	int err;

	if (!aupp)
		return EINVAL;

	rp = mem_zalloc(sizeof(*rp), destructor);
	if (!rp)
		return ENOMEM;

	rp->cfg = cfg;
	rp->srate = cfg->srate_play;
	rp->ch    = cfg->channels_play;
	rp->fmt   = cfg->play_fmt;
	rp->sampvsz = sampc * aufmt_sample_size(rp->fmt);
	rp->sampv   = mem_zalloc(rp->sampvsz, NULL);
	if (!rp->sampv) {
		err = ENOMEM;
		goto out;
	}

	err = mutex_alloc(&rp->mtx);

out:
	if (err)
		rp = mem_deref(rp);
	else
		*aupp = rp;

	return err;
}


void aup_flush(struct aurpipe *rp)
{
	if (!rp || !re_atomic_rlx(&rp->ready))
		return;

	mtx_lock(rp->mtx);
	aubuf_flush(rp->aubuf);

	/* Reset audio filter chain */
	list_flush(&rp->filtl);
	mtx_unlock(rp->mtx);
}


int aup_decoder_set(struct aurpipe *rp,
		    const struct aucodec *ac, const char *params)
{
	int err = 0;

	if (!rp || !ac)
		return EINVAL;

	info("audio: Set audio decoder: %s %uHz %dch\n",
	     ac->name, ac->srate, ac->ch);

	mtx_lock(rp->mtx);
	if (ac != rp->ac) {
		rp->ac = ac;
		rp->dec = mem_deref(rp->dec);
	}

	if (ac->decupdh) {
		err = ac->decupdh(&rp->dec, ac, params);
		if (err) {
			warning("aurpipe: alloc decoder: %m\n", err);
			goto out;
		}
	}

out:
	mtx_unlock(rp->mtx);
	return err;
}


int aup_filt_append(struct aurpipe *rp, struct aufilt_dec_st *decst)
{
	if (!rp || !decst)
		return EINVAL;

	mtx_lock(rp->mtx);
	list_append(&rp->filtl, &decst->le, decst);
	mtx_unlock(rp->mtx);

	return 0;
}


bool aup_filt_empty(const struct aurpipe *rp)
{
	bool empty;
	if (!rp)
		return false;

	mtx_lock(rp->mtx);
	empty = list_isempty(&rp->filtl);
	mtx_unlock(rp->mtx);

	return empty;
}


bool aup_level_set(const struct aurpipe *rp)
{
	bool set;
	if (!rp)
		return false;

	mtx_lock(rp->mtx);
	set = rp->level_set;
	mtx_unlock(rp->mtx);

	return set;
}


double aup_level(const struct aurpipe *rp)
{
	double v;
	if (!rp)
		return 0.0;

	mtx_lock(rp->mtx);
	v = rp->level_last;
	mtx_unlock(rp->mtx);

	return v;
}


const struct aucodec *aup_codec(const struct aurpipe *rp)
{
	const struct aucodec *ac;

	if (!rp)
		return NULL;

	mtx_lock(rp->mtx);
	ac = rp->ac;
	mtx_unlock(rp->mtx);
	return ac;
}


void aup_read(struct aurpipe *rp, struct auframe *af)
{
	if (!rp || !re_atomic_rlx(&rp->ready))
		return;

	aubuf_read_auframe(rp->aubuf, af);
}


void aup_stop(struct aurpipe *rp)
{
	if (!rp)
		return;

	mtx_lock(rp->mtx);
	rp->ac = NULL;
	mtx_unlock(rp->mtx);
}


bool aup_started(const struct aurpipe *rp)
{
	if (!rp || !re_atomic_rlx(&rp->ready))
		return false;

	return aubuf_started(rp->aubuf);
}


int aup_debug(struct re_printf *pf, const struct aurpipe *rp)
{
	struct mbuf *mb;
	uint64_t bpms;
	int err;

	if (!rp || !re_atomic_rlx(&rp->ready))
		return 0;

	mb = mbuf_alloc(32);
	if (!mb)
		return ENOMEM;

	mtx_lock(rp->mtx);
	bpms = rp->srate * rp->ch * aufmt_sample_size(rp->fmt) / 1000;
	err  = mbuf_printf(mb,
			   " rx:   decode: %H %s\n",
			   aucodec_print, rp->ac,
			   aufmt_name(rp->fmt));
	err |= mbuf_printf(mb, "       aubuf: %H"
			   " (cur %.2fms, max %.2fms)\n",
			   aubuf_debug, rp->aubuf,
			   aubuf_cur_size(rp->aubuf) / bpms,
			   aubuf_maxsz(rp->aubuf) / bpms);
	err |= mbuf_printf(mb, "       n_discard:%llu\n",
			   rp->stats.n_discard);
	if (rp->level_set) {
		err |= mbuf_printf(mb, "       level %.3f dBov\n",
				   rp->level_last);
	}
	if (rp->ts_recv.is_set) {
		err |= mbuf_printf(mb, "       time = %.3f sec\n",
				   aup_calc_seconds(rp));
	}
	else {
		err |= mbuf_printf(mb, "       time = (not started)\n");
	}
	mtx_unlock(rp->mtx);

	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);
out:
	mem_deref(mb);
	return err;
}


int aup_print_pipeline(struct re_printf *pf, const struct aurpipe *rp)
{
	struct mbuf *mb;
	struct le *le;
	int err;

	if (!rp)
		return 0;

	mb = mbuf_alloc(32);
	if (!mb)
		return ENOMEM;

	err = mbuf_printf(mb, " <--- aubuf");
	mtx_lock(rp->mtx);
	for (le = list_head(&rp->filtl); le; le = le->next) {
		struct aufilt_dec_st *st = le->data;

		if (st->af->dech)
			err |= mbuf_printf(mb, " <--- %s", st->af->name);
	}

	err |= mbuf_printf(mb, " <--- %s\n",
			  rp->ac ? rp->ac->name : "(decoder)");
	mtx_unlock(rp->mtx);

	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);
out:
	mem_deref(mb);
	return err;
}
