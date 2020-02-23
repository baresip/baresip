/**
 * @file avformat/audio.c  libavformat media-source -- audio
 *
 * Copyright (C) 2010 - 2020 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <pthread.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include "mod_avformat.h"


struct ausrc_st {
	const struct ausrc *as;  /* base class */

	struct shared *shared;
	ausrc_read_h *readh;
	ausrc_error_h *errh;
	void *arg;
};


static void audio_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	avformat_shared_set_audio(st->shared, NULL);
	mem_deref(st->shared);
}


static enum AVSampleFormat aufmt_to_avsampleformat(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE: return AV_SAMPLE_FMT_S16;
	case AUFMT_FLOAT: return AV_SAMPLE_FMT_FLT;
	default:          return AV_SAMPLE_FMT_NONE;
	}
}


int avformat_audio_alloc(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *dev,
			 ausrc_read_h *readh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct shared *sh;
	enum AVSampleFormat format;
	int err = 0;

	if (!stp || !as || !prm || !readh)
		return EINVAL;

	info("avformat: audio: loading input file '%s'\n", dev);

	format = aufmt_to_avsampleformat(prm->fmt);
	if (format == AV_SAMPLE_FMT_NONE) {
		warning("avformat: audio: unsupported sampleformat (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), audio_destructor);
	if (!st)
		return ENOMEM;

	st->as    = as;
	st->readh = readh;
	st->errh  = errh;
	st->arg   = arg;

	/* todo: also lookup "dev" ? */
	if (ctx && *ctx && (*ctx)->id && !strcmp((*ctx)->id, "avformat")) {
		st->shared = mem_ref(*ctx);
	}
	else {
		err = avformat_shared_alloc(&st->shared, dev);
		if (err)
			goto out;

		if (ctx)
			*ctx = (struct media_ctx *)st->shared;
	}

	sh = st->shared;

	if (st->shared->au.idx < 0 || !st->shared->au.ctx) {
		info("avformat: audio: media file has no audio stream\n");
		err = ENOENT;
		goto out;
	}

	if (sh->au.ctx->sample_rate != (int)prm->srate ||
	    sh->au.ctx->channels != prm->ch) {
		info("avformat: audio: samplerate/channels"
		     " mismatch: param=%u/%u, file=%u/%u\n",
		     prm->srate, prm->ch,
		     sh->au.ctx->sample_rate, sh->au.ctx->channels);
		err = ENOTSUP;
		goto out;
	}

	if (format != sh->au.ctx->sample_fmt) {
		info("avformat: audio: sample format mismatch:"
		     " param=%s, file=%s\n",
		     av_get_sample_fmt_name(format),
		     av_get_sample_fmt_name(sh->au.ctx->sample_fmt));
		err = ENOTSUP;
		goto out;
	}

	avformat_shared_set_audio(st->shared, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


void avformat_audio_decode(struct shared *st, AVPacket *pkt)
{
	AVFrame frame;
	int ret;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 37, 100)
	int got_frame;
#endif

	if (!st || !st->au.ctx)
		return;

	memset(&frame, 0, sizeof(frame));

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

	ret = avcodec_send_packet(st->au.ctx, pkt);
	if (ret < 0)
		return;

	ret = avcodec_receive_frame(st->au.ctx, &frame);
	if (ret < 0)
		return;

#else
	ret = avcodec_decode_audio4(st->au.ctx, &frame, &got_frame, pkt);
	if (ret < 0 || !got_frame)
		return;
#endif

	/* NOTE: pass timestamp to application */

	lock_read_get(st->lock);

	if (st->ausrc_st && st->ausrc_st->readh) {
		st->ausrc_st->readh(frame.data[0],
				    frame.nb_samples * frame.channels,
				    st->ausrc_st->arg);
	}

	lock_rel(st->lock);

	av_frame_unref(&frame);
}
