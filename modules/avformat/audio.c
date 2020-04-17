/**
 * @file avformat/audio.c  libavformat media-source -- audio
 *
 * Copyright (C) 2010 - 2020 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <pthread.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include "mod_avformat.h"


struct ausrc_st {
	const struct ausrc *as;  /* base class */

	struct shared *shared;
	struct ausrc_prm prm;
	SwrContext *swr;
	ausrc_read_h *readh;
	ausrc_error_h *errh;
	void *arg;
};


static void audio_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	avformat_shared_set_audio(st->shared, NULL);
	mem_deref(st->shared);

	if (st->swr)
		swr_free(&st->swr);
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
	int err = 0;

	if (!stp || !as || !prm || !readh)
		return EINVAL;

	info("avformat: audio: loading input file '%s'\n", dev);

	st = mem_zalloc(sizeof(*st), audio_destructor);
	if (!st)
		return ENOMEM;

	st->as    = as;
	st->readh = readh;
	st->errh  = errh;
	st->arg   = arg;
	st->prm   = *prm;

	if (ctx && *ctx && (*ctx)->id && !strcmp((*ctx)->id, "avformat")) {
		st->shared = mem_ref(*ctx);
	}
	else {
		err = avformat_shared_alloc(&st->shared, dev,
					    0.0, NULL, false);
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

	st->swr = swr_alloc();
	if (!st->swr) {
		err = ENOMEM;
		goto out;
	}

	avformat_shared_set_audio(st->shared, st);

	info("avformat: audio: converting %u/%u %s -> %u/%u %s\n",
	     sh->au.ctx->sample_rate, sh->au.ctx->channels,
	     av_get_sample_fmt_name(sh->au.ctx->sample_fmt),
	     prm->srate, prm->ch, aufmt_name(prm->fmt));

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
	AVFrame frame2;
	int ret;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 37, 100)
	int got_frame;
#endif

	if (!st || !st->au.ctx)
		return;

	memset(&frame, 0, sizeof(frame));
	memset(&frame2, 0, sizeof(frame2));

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

		const AVRational tb = st->au.time_base;
		struct auframe af;

		frame.channel_layout =
			av_get_default_channel_layout(frame.channels);

		frame2.channels       = st->ausrc_st->prm.ch;
		frame2.channel_layout =
			av_get_default_channel_layout(st->ausrc_st->prm.ch);
		frame2.sample_rate    = st->ausrc_st->prm.srate;
		frame2.format         =
			aufmt_to_avsampleformat(st->ausrc_st->prm.fmt);

		ret = swr_convert_frame(st->ausrc_st->swr, &frame2, &frame);
		if (ret) {
			warning("avformat: swr_convert_frame failed (%d)\n",
				ret);
			goto unlock;
		}

		auframe_init(&af, st->ausrc_st->prm.fmt, frame2.data[0],
			     frame2.nb_samples * frame2.channels);
		af.timestamp = frame.pts * AUDIO_TIMEBASE * tb.num / tb.den;

		st->ausrc_st->readh(&af, st->ausrc_st->arg);
	}

 unlock:
	lock_rel(st->lock);

	av_frame_unref(&frame2);
	av_frame_unref(&frame);
}
