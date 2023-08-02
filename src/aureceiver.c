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
	struct audio_recv *ar = arg;

	mem_deref(ar->dec);
	mem_deref(ar->aubuf);
	mem_deref(ar->sampv);
	mem_deref(ar->mtx);
	mem_deref(ar->aubuf_mtx);
	list_flush(&ar->filtl);
}


static int aup_process_decfilt(struct audio_recv *ar, struct auframe *af)
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


static double aup_calc_seconds(const struct audio_recv *ar)
{
	uint64_t dur;
	double seconds;

	if (!ar->ac)
		return .0;

	dur = timestamp_duration(&ar->ts_recv);
	seconds = timestamp_calc_seconds(dur, ar->ac->crate);

	return seconds;
}


static int aup_alloc_aubuf(struct audio_recv *ar, const struct auframe *af)
{
	size_t min_sz;
	size_t max_sz;
	size_t sz;
	const struct config_audio *cfg = ar->cfg;
	int err;

	sz = aufmt_sample_size(cfg->play_fmt);
	min_sz = sz * calc_nsamp(af->srate, af->ch, cfg->buffer.min);
	max_sz = sz * calc_nsamp(af->srate, af->ch, cfg->buffer.max);

	debug("audio_recv: create audio buffer"
	      " [%u - %u ms]"
	      " [%zu - %zu bytes]\n",
	      (unsigned) cfg->buffer.min, (unsigned) cfg->buffer.max,
	      min_sz, max_sz);

	err = aubuf_alloc(&ar->aubuf, min_sz, max_sz);
	if (err) {
		warning("audio_recv: aubuf alloc error (%m)\n",
			err);
	}

	aubuf_set_mode(ar->aubuf, cfg->adaptive ?
		       AUBUF_ADAPTIVE : AUBUF_FIXED);
	aubuf_set_silence(ar->aubuf, cfg->silence);
	return err;
}


static int aup_push_aubuf(struct audio_recv *ar, const struct auframe *af)
{
	int err;
	uint64_t bpms;

	if (!ar->aubuf) {
		mtx_lock(ar->aubuf_mtx);
		err = aup_alloc_aubuf(ar, af);
		mtx_unlock(ar->aubuf_mtx);
		if (err)
			return err;
	}

	err = aubuf_write_auframe(ar->aubuf, af);
	if (err)
		return err;

	ar->srate = af->srate;
	ar->ch    = af->ch;
	ar->fmt   = af->fmt;

	bpms = ar->srate * ar->ch * aufmt_sample_size(ar->fmt) / 1000;
	if (bpms)
		re_atomic_rlx_set(&ar->stats.latency,
				  aubuf_cur_size(ar->aubuf) / bpms);

	return 0;
}


static int aup_stream_decode(struct audio_recv *ar,
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
			warning("audio: %s codec decode %u bytes: %m\n",
				ac->name, mbuf_get_left(mb), err);
			goto out;
		}
	}
	else if (mbuf_get_left(mb)) {

		err = ac->dech(ar->dec,
				   ar->fmt, ar->sampv, &sampc,
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

	auframe_init(&af, ar->fmt, ar->sampv, sampc, ac->srate, ac->ch);
	af.timestamp = ((uint64_t) hdr->ts) * AUDIO_TIMEBASE / ac->crate;

	if (drop) {
		aubuf_drop_auframe(ar->aubuf, &af);
		goto out;
	}

	if (flush)
		aubuf_flush(ar->aubuf);

	err = aup_process_decfilt(ar, &af);
	if (err)
		goto out;

	err = aup_push_aubuf(ar, &af);
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
void aup_receive(struct audio_recv *ar, const struct rtp_header *hdr,
		 struct rtpext *extv, size_t extc,
		 struct mbuf *mb, unsigned lostc, bool *ignore)
{
	bool discard = false;
	bool drop = *ignore;
	int wrap;
	(void) lostc;

	if (!mb)
		goto out;

	mtx_lock(ar->mtx);
	if (hdr->pt == ar->telev_pt) {
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
		warning("audio: rtp timestamp wraps backwards"
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
		goto unlock;
	}

 out:
	/* TODO:  what if lostc > 1 ?*/
	/* PLC should generate lostc frames here. Not only one.
	 * aubuf should replace PLC frames with late arriving real frames.
	 * It should use timestamp to decide if a frame should be replaced. */
/*        if (lostc)*/
/*                (void)aup_stream_decode(ar, hdr, mb, lostc, drop);*/

	(void)aup_stream_decode(ar, hdr, mb, 0, drop);

unlock:
	mtx_unlock(ar->mtx);
}


void aup_set_extmap(struct audio_recv *ar, uint8_t aulevel)
{
	if (!ar)
		return;

	mtx_lock(ar->mtx);
	ar->extmap_aulevel = aulevel;
	mtx_unlock(ar->mtx);
}


void aup_set_telev_pt(struct audio_recv *ar, int pt)
{
	if (!ar)
		return;

	mtx_lock(ar->mtx);
	ar->telev_pt = pt;
	mtx_unlock(ar->mtx);
}


uint64_t aup_latency(const struct audio_recv *ar)
{
	if (!ar)
		return 0;

	return re_atomic_rlx(&ar->stats.latency);
}


int aup_alloc(struct audio_recv **aupp, const struct config_audio *cfg,
	      size_t sampc)
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
	ar->fmt   = cfg->play_fmt;
	ar->sampvsz = sampc * aufmt_sample_size(ar->fmt);
	ar->sampv   = mem_zalloc(ar->sampvsz, NULL);
	if (!ar->sampv) {
		err = ENOMEM;
		goto out;
	}

	err  = mutex_alloc(&ar->mtx);
	err |= mutex_alloc(&ar->aubuf_mtx);

out:
	if (err)
		ar = mem_deref(ar);
	else
		*aupp = ar;

	return err;
}


void aup_flush(struct audio_recv *ar)
{
	if (!ar)
		return;

	mtx_lock(ar->mtx);
	aubuf_flush(ar->aubuf);

	/* Reset audio filter chain */
	list_flush(&ar->filtl);
	mtx_unlock(ar->mtx);
}


int aup_decoder_set(struct audio_recv *ar,
		    const struct aucodec *ac, const char *params)
{
	int err = 0;

	if (!ar || !ac)
		return EINVAL;

	info("audio: Set audio decoder: %s %uHz %dch\n",
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

out:
	mtx_unlock(ar->mtx);
	return err;
}


int aup_filt_append(struct audio_recv *ar, struct aufilt_dec_st *decst)
{
	if (!ar || !decst)
		return EINVAL;

	mtx_lock(ar->mtx);
	list_append(&ar->filtl, &decst->le, decst);
	mtx_unlock(ar->mtx);

	return 0;
}


bool aup_filt_empty(const struct audio_recv *ar)
{
	bool empty;
	if (!ar)
		return false;

	mtx_lock(ar->mtx);
	empty = list_isempty(&ar->filtl);
	mtx_unlock(ar->mtx);

	return empty;
}


bool aup_level_set(const struct audio_recv *ar)
{
	bool set;
	if (!ar)
		return false;

	mtx_lock(ar->mtx);
	set = ar->level_set;
	mtx_unlock(ar->mtx);

	return set;
}


double aup_level(const struct audio_recv *ar)
{
	double v;
	if (!ar)
		return 0.0;

	mtx_lock(ar->mtx);
	v = ar->level_last;
	mtx_unlock(ar->mtx);

	return v;
}


const struct aucodec *aup_codec(const struct audio_recv *ar)
{
	const struct aucodec *ac;

	if (!ar)
		return NULL;

	mtx_lock(ar->mtx);
	ac = ar->ac;
	mtx_unlock(ar->mtx);
	return ac;
}


void aup_read(struct audio_recv *ar, struct auframe *af)
{
	if (!ar || mtx_trylock(ar->aubuf_mtx) != thrd_success)
		return;

	aubuf_read_auframe(ar->aubuf, af);
	mtx_unlock(ar->aubuf_mtx);
}


void aup_stop(struct audio_recv *ar)
{
	if (!ar)
		return;

	mtx_lock(ar->mtx);
	ar->ac = NULL;
	mtx_unlock(ar->mtx);
}


bool aup_started(const struct audio_recv *ar)
{
	bool ret;

	if (!ar || mtx_trylock(ar->aubuf_mtx) != thrd_success)
		return false;

	ret = aubuf_started(ar->aubuf);
	mtx_unlock(ar->aubuf_mtx);
	return ret;
}


int aup_debug(struct re_printf *pf, const struct audio_recv *ar)
{
	struct mbuf *mb;
	uint64_t bpms;
	int err;

	if (!ar || mtx_trylock(ar->aubuf_mtx) != thrd_success)
		return 0;

	mb = mbuf_alloc(32);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	mtx_lock(ar->mtx);
	bpms = ar->srate * ar->ch * aufmt_sample_size(ar->fmt) / 1000;
	err  = mbuf_printf(mb,
			   " rx:   decode: %H %s\n",
			   aucodec_print, ar->ac,
			   aufmt_name(ar->fmt));
	err |= mbuf_printf(mb, "       aubuf: %H"
			   " (cur %.2fms, max %.2fms)\n",
			   aubuf_debug, ar->aubuf,
			   aubuf_cur_size(ar->aubuf) / bpms,
			   aubuf_maxsz(ar->aubuf) / bpms);
	err |= mbuf_printf(mb, "       n_discard:%llu\n",
			   ar->stats.n_discard);
	if (ar->level_set) {
		err |= mbuf_printf(mb, "       level %.3f dBov\n",
				   ar->level_last);
	}
	if (ar->ts_recv.is_set) {
		err |= mbuf_printf(mb, "       time = %.3f sec\n",
				   aup_calc_seconds(ar));
	}
	else {
		err |= mbuf_printf(mb, "       time = (not started)\n");
	}
	mtx_unlock(ar->mtx);

	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);
out:
	mem_deref(mb);
	mtx_unlock(ar->aubuf_mtx);
	return err;
}


int aup_print_pipeline(struct re_printf *pf, const struct audio_recv *ar)
{
	struct mbuf *mb;
	struct le *le;
	int err;

	if (!ar)
		return 0;

	mb = mbuf_alloc(32);
	if (!mb)
		return ENOMEM;

	err = mbuf_printf(mb, " <--- aubuf");
	mtx_lock(ar->mtx);
	for (le = list_head(&ar->filtl); le; le = le->next) {
		struct aufilt_dec_st *st = le->data;

		if (st->af->dech)
			err |= mbuf_printf(mb, " <--- %s", st->af->name);
	}

	err |= mbuf_printf(mb, " <--- %s\n",
			  ar->ac ? ar->ac->name : "(decoder)");
	mtx_unlock(ar->mtx);

	if (err)
		goto out;

	err = re_hprintf(pf, "%b", mb->buf, mb->pos);
out:
	mem_deref(mb);
	return err;
}
