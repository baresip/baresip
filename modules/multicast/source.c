/**
 * @file source.c
 *
 * Copyright (C) 2021 Commend.com - c.huber@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include <stdlib.h>

#include "multicast.h"

#define DEBUG_MODULE "mcsource"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


/**
 * Multicast source struct
 *
 * Contains configuration of the audio source and buffer for the audio data
 */
struct mcsource {
	struct config_audio *cfg;
	struct ausrc_st *ausrc;
	struct ausrc_prm ausrc_prm;
	const struct aucodec *ac;
	struct auenc_state *enc;
	enum aufmt src_fmt;
	enum aufmt enc_fmt;

	void *sampv;
	struct aubuf *aubuf;
	size_t aubuf_maxsz;
	volatile bool aubuf_started;
	struct auresamp resamp;
	int16_t *sampv_rs;
	struct list filtl;

	struct mbuf *mb;
	uint32_t ptime;
	uint64_t ts_ext;
	uint32_t ts_base;
	size_t psize;
	bool marker;

	char *module;
	char *device;

	mcsender_send_h *sendh;
	void *arg;

	struct {
		thrd_t tid;
		bool run;
	} thr;
};


static void mcsource_destructor(void *arg)
{
	struct mcsource *src = arg;

	switch (src->cfg->txmode) {
		case AUDIO_MODE_THREAD:
			if (src->thr.run) {
				src->thr.run = false;
				thrd_join(src->thr.tid, NULL);
			}
		default:
			break;
	}

	src->ausrc = mem_deref(src->ausrc);
	src->aubuf = mem_deref(src->aubuf);
	list_flush(&src->filtl);

	src->enc      = mem_deref(src->enc);
	src->mb       = mem_deref(src->mb);
	src->sampv    = mem_deref(src->sampv);
	src->sampv_rs = mem_deref(src->sampv_rs);

	src->module   = mem_deref(src->module);
	src->device   = mem_deref(src->device);
}


/**
 * Encode and send audio data via multicast send handler of src
 *
 * @note This function has REAL-TIME properties
 *
 * @param src   Multicast source object
 * @param sampv Samplebuffer
 * @param sampc Samplecounter
 */
static void encode_rtp_send(struct mcsource *src, uint16_t *sampv,
	size_t sampc)
{
	size_t frame_size;
	size_t sampc_rtp;
	size_t len;

	size_t ext_len = 0;
	uint32_t ts_delta = 0;
	int err = 0;

	if (!src->ac || !src->ac->ench)
		return;

	src->mb->pos = src->mb->end = STREAM_PRESZ;

	len = mbuf_get_space(src->mb);
	err = src->ac->ench(src->enc, &src->marker, mbuf_buf(src->mb), &len,
		src->enc_fmt, sampv, sampc);

	if ((err & 0xffff0000) == 0x00010000) {
		ts_delta = err & 0xffff;
		sampc = 0;
	}
	else if (err) {
		warning ("multicast send: &s encode error: &d samples (%m)\n",
			src->ac->name, sampc, err);
		goto out;
	}

	src->mb->pos = STREAM_PRESZ;
	src->mb->end = STREAM_PRESZ + ext_len + len;

	if (mbuf_get_left(src->mb)) {
		uint32_t rtp_ts = src->ts_ext & 0xffffffff;

		if (len) {
			err = src->sendh(ext_len, src->marker,
				rtp_ts, src->mb, src->arg);
			if (err)
				goto out;
		}

		if (ts_delta) {
			src->ts_ext += ts_delta;
			goto out;
		}
	}

	sampc_rtp = sampc * src->ac->crate / src->ac->srate;
	frame_size = sampc_rtp / src->ac->ch;
	src->ts_ext += (uint32_t) frame_size;

  out:
	src->marker = false;
}


/**
 * Poll timed read from audio buffer
 *
 * @note This function has REAL-TIME properties
 *
 * @param src Multicast source object
 */
static void poll_aubuf_tx(struct mcsource *src)
{
	struct auframe af;
	int16_t *sampv = src->sampv;
	size_t sampc;
	size_t sz;
	size_t num_bytes;
	struct le *le;
	uint32_t srate;
	uint8_t ch;
	int err = 0;

	sz = aufmt_sample_size(src->src_fmt);
	if (!sz)
		return;

	num_bytes = src->psize;
	sampc = num_bytes / sz;

	if (src->src_fmt == AUFMT_S16LE) {
		aubuf_read(src->aubuf, (uint8_t *)sampv, num_bytes);
	}
	else if (src->enc_fmt == AUFMT_S16LE) {
		void *tmp_sampv = NULL;

		tmp_sampv = mem_zalloc(num_bytes, NULL);
		if (!tmp_sampv)
			return;

		aubuf_read(src->aubuf, tmp_sampv, num_bytes);
		auconv_to_s16(sampv, src->src_fmt, tmp_sampv, sampc);
		mem_deref(tmp_sampv);
	}
	else {
		warning("multicast send: invalid sample formats (%s -> %s)\n",
			aufmt_name(src->src_fmt),
			aufmt_name(src->enc_fmt));
	}

	if (src->resamp.resample) {
		size_t sampc_rs = AUDIO_SAMPSZ;

		if (src->enc_fmt != AUFMT_S16LE) {
			warning("multicast send: skipping resampler due to"
				" incompatible format (%s)\n",
				aufmt_name(src->enc_fmt));
			return;
		}

		err = auresamp(&src->resamp, src->sampv_rs, &sampc_rs,
			src->sampv, sampc);
			if (err)
				return;

		sampv = src->sampv_rs;
		sampc = sampc_rs;
	}

	if (src->resamp.resample) {
		srate = src->resamp.irate;
		ch = src->resamp.ich;
	}
	else {
		srate = src->ausrc_prm.srate;
		ch = src->ausrc_prm.ch;
	}

	auframe_init(&af, src->enc_fmt, sampv, sampc, srate, ch);

	/* process exactly one audio-frame in list order */
	for (le = src->filtl.head; le; le = le->next) {
		struct aufilt_enc_st * st = le->data;

		if (st->af && st->af->ench)
			err |= st->af->ench(st, &af);
	}

	if (err)
		warning("multicast source: aufilter encode (%m)\n", err);

	encode_rtp_send(src, af.sampv, af.sampc);
}


/**
 * Audio source error handler
 *
 * @param err Error code
 * @param str Error string
 * @param arg Multicast source object
 */
static void ausrc_error_handler(int err, const char *str, void *arg)
{
	(void) err;
	(void) str;
	(void) arg;
}


/**
 * Audio source read handler
 *
 * @note This function has REAL-TIME properties
 *
 * @param af  Audio frame
 * @param arg Multicast source object
 */
static void ausrc_read_handler(struct auframe *af, void *arg)
{
	struct mcsource *src = arg;
	size_t num_bytes = auframe_size(af);

	if (src->src_fmt != af->fmt) {
		warning ("multicast source: ausrc format mismatch: "
			"expected=%d(%s), actual=%d(%s)\n",
			src->src_fmt, aufmt_name(src->src_fmt),
			af->fmt, aufmt_name(af->fmt));
		return;
	}

	(void) aubuf_write(src->aubuf, af->sampv, num_bytes);
	src->aubuf_started = true;

	if (src->cfg->txmode == AUDIO_MODE_POLL) {
		unsigned i;

		for (i = 0; i < 16; i++) {
			if (aubuf_cur_size(src->aubuf) < src->psize)
				break;

			poll_aubuf_tx(src);
		}
	}
}


/**
 * Standalone transmitter thread function
 *
 * @param arg Multicast source object
 *
 * @return NULL
 */
static int tx_thread(void *arg)
{
	struct mcsource *src = arg;
	uint64_t ts = 0;

	while (src->thr.run) {
		uint64_t now;
		sys_msleep(4);

		if (!src->aubuf_started)
			continue;

		if (!src->thr.run)
			break;

		now = tmr_jiffies();
		if (!ts)
			ts = now;

		if (ts > now)
			continue;

		if (aubuf_cur_size(src->aubuf) >= src->psize)
			poll_aubuf_tx(src);

		ts += src->ptime;
	}

	return 0;
}


/**
 * Start audio source
 *
 * @param src Multicast source object
 *
 * @return 0 if success, otherwise errorcode
 */
static int start_source(struct mcsource *src)
{
	int err = 0;
	uint32_t srate_dsp;
	uint32_t channels_dsp;
	bool resamp = false;

	if (!src)
		return EINVAL;

	srate_dsp = src->ac->srate;
	channels_dsp = src->ac->ch;

	if (src->cfg->srate_src && src->cfg->srate_src != srate_dsp) {
		resamp = true;
		srate_dsp = src->cfg->srate_src;
	}
	if (src->cfg->channels_src && src->cfg->channels_src != channels_dsp) {
		resamp = true;
		channels_dsp = src->cfg->channels_src;
	}

	if (resamp && src->sampv_rs) {
		src->sampv_rs = mem_zalloc(
			AUDIO_SAMPSZ * sizeof(int16_t), NULL);
		if (!src->sampv_rs)
			return ENOMEM;

		err = auresamp_setup(&src->resamp, srate_dsp, channels_dsp,
			src->ac->srate, src->ac->ch);
		if (err) {
			warning ("multicast source: could not setup ausrc "
				"resample (%m)\n", err);
			return err;
		}
	}

	if (!src->ausrc && ausrc_find(baresip_ausrcl(), NULL)) {
		struct ausrc_prm prm;
		size_t sz;

		prm.srate = srate_dsp;
		prm.ch = channels_dsp;
		prm.ptime = src->ptime;
		prm.fmt = src->src_fmt;

		sz = aufmt_sample_size(src->src_fmt);
		src->psize = sz * (prm.srate * prm.ch * prm.ptime / 1000);
		src->aubuf_maxsz = src->psize * 30;
		if (!src->aubuf) {
			err = aubuf_alloc(&src->aubuf, src->psize,
				src->aubuf_maxsz);
			if (err)
				return err;
		}

		err = ausrc_alloc(&src->ausrc, baresip_ausrcl(),
			src->module, &prm, src->device,
			ausrc_read_handler, ausrc_error_handler, src);
		if (err) {
			warning ("multicast source: start_source faild (%s-%s)"
				" (%m)\n", src->module, src->device, err);
			return err;
		}

		switch (src->cfg->txmode) {
			case AUDIO_MODE_POLL:
				break;
			case AUDIO_MODE_THREAD:
				if (!src->thr.run) {
					src->thr.run = true;
					err = thread_create_name(&src->thr.tid,
						"multicast", tx_thread, src);
					if (err) {
						src->thr.run = false;
						return err;
					}
				}
				break;

			default:
				warning ("multicast source: tx mode "
					"not supported (%d)\n",
					src->cfg->txmode);
				return ENOTSUP;
		}

		src->ausrc_prm = prm;
		info ("multicast source: source started with sample format "
			"%s\n", aufmt_name(src->src_fmt));
	}

	return err;
}


/**
 * Setup all available audio filter for the encoder
 *
 * @param src     Multicast source object
 * @param aufiltl List of audio filter
 *
 * @return 0 if success, otherwise errorcode
 */
static int aufilt_setup(struct mcsource *src, struct list *aufiltl)
{
	struct aufilt_prm prm;
	struct le *le;
	int err = 0;

	if (!src->ac)
		return 0;

	if (!list_isempty(&src->filtl))
		return 0;

	prm.srate = src->ac->srate;
	prm.ch = src->ac->ch;
	prm.fmt = src->enc_fmt;

	for (le = list_head(aufiltl); le; le = le->next) {
		struct aufilt *af = le->data;
		struct aufilt_enc_st *encst = NULL;
		void *ctx = NULL;

		if (af->encupdh) {
			err = af->encupdh(&encst, &ctx, af, &prm, NULL);
			if (err) {
				warning("multicast source: erro in encoder"
					"autio-filter '%s' (%m)\n",
					af->name, err);
			}
			else {
				encst->af = af;
				list_append(&src->filtl, &encst->le,
					encst);
			}
		}

		if (err) {
			warning("multicast source: audio-filter '%s' "
				"update failed (%m)\n", af->name, err);
			break;
		}
	}

	return err;
}


/**
 * Start multicast source
 *
 * @param srcp  Multicast source ptr
 * @param ac    Audio codec
 * @param sendh Send handler ptr
 * @param arg   Send handler Argument
 *
 * @return 0 if success, otherwise errorcode
 */
int mcsource_start(struct mcsource **srcp, const struct aucodec *ac,
	mcsender_send_h *sendh, void *arg)
{
	int err = 0;
	struct mcsource *src = NULL;
	struct config_audio *cfg = &conf_config()->audio;

	if (!srcp || !ac)
		return EINVAL;

	src = mem_zalloc(sizeof(*src), mcsource_destructor);
	if (!src)
		return ENOMEM;

	src->cfg = cfg;
	src->sendh = sendh;
	src->arg = arg;

	src->src_fmt = cfg->src_fmt;
	src->enc_fmt = cfg->enc_fmt;
	src->mb = mbuf_alloc(STREAM_PRESZ + 4096);
	src->sampv = mem_zalloc(
		AUDIO_SAMPSZ * aufmt_sample_size(src->enc_fmt), NULL);
	if (!src->mb || !src->sampv) {
		err = ENOMEM;
		goto out;
	}

	auresamp_init(&src->resamp);
	src->ptime = PTIME;
	src->ts_ext = src->ts_base = rand_u16();
	src->marker = true;

	err = str_dup(&src->module, cfg->src_mod);
	err |= str_dup(&src->device, cfg->src_dev);
	if (err)
		goto out;

	src->ac = ac;
	if (src->ac->encupdh) {
		struct auenc_param prm;

		prm.bitrate = 0;

		err = src->ac->encupdh(&src->enc, src->ac, &prm, NULL);
		if (err) {
			warning ("multicast source: alloc encoder (%m)\n",
				err);
			goto out;
		}
	}

	err = aufilt_setup(src, baresip_aufiltl());
	if (err)
		goto out;

	err = start_source(src);
	if (err)
		goto out;

  out:
	if (err)
		mem_deref(src);
	else
		*srcp = src;

	return err;
}


/**
 * Stop one multicast source.
 *
 * @param unused Multicast audio source object
 */
void mcsource_stop(struct mcsource *unused)
{
	(void) unused;
}


/**
 * Initialize everything needed for the source beforhand
 *
 * @return 0 if success, otherwise errorcode
 */
int mcsource_init(void)
{
	return 0;
}


/**
 * Terminate everything needed for the source afterwards
 *
 */
void mcsource_terminate(void)
{
	return;
}
