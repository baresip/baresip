/**
 * @file avformat/video.c  libavformat media-source -- video
 *
 * Copyright (C) 2010 - 2020 Alfred E. Heggestad
 * Copyright (C) 2021 by:
 *     Media Magic Technologies <developer@mediamagictechnologies.com>
 *     and Divus GmbH <developer@divus.eu>
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <pthread.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include "mod_avformat.h"


struct vidsrc_st {
	struct shared *shared;
	vidsrc_frame_h *frameh;
	vidsrc_packet_h *packeth;
	void *arg;
};


static void video_destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	avformat_shared_set_video(st->shared, NULL);
	mem_deref(st->shared);
}


static enum vidfmt avpixfmt_to_vidfmt(enum AVPixelFormat pix_fmt)
{
	switch (pix_fmt) {

	case AV_PIX_FMT_YUV420P:  return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUVJ420P: return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUV444P:  return VID_FMT_YUV444P;
	case AV_PIX_FMT_NV12:     return VID_FMT_NV12;
	case AV_PIX_FMT_NV21:     return VID_FMT_NV21;
	case AV_PIX_FMT_UYVY422:  return VID_FMT_UYVY422;
	case AV_PIX_FMT_YUYV422:  return VID_FMT_YUYV422;
	default:                  return (enum vidfmt)-1;
	}
}


int avformat_video_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
			 struct media_ctx **ctx, struct vidsrc_prm *prm,
			 const struct vidsz *size, const char *fmt,
			 const char *dev, vidsrc_frame_h *frameh,
			 vidsrc_packet_h *packeth,
			 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err = 0;

	(void)fmt;
	(void)packeth;
	(void)errorh;

	if (!stp || !vs || !prm || !size || !frameh)
		return EINVAL;

	debug("avformat: video: alloc dev='%s'\n", dev);

	st = mem_zalloc(sizeof(*st), video_destructor);
	if (!st)
		return ENOMEM;

	st->frameh = frameh;
	st->packeth = packeth;
	st->arg    = arg;

	if (ctx && *ctx && (*ctx)->id && !strcmp((*ctx)->id, "avformat")) {
		st->shared = mem_ref(*ctx);
	}
	else {
		err = avformat_shared_alloc(&st->shared, dev,
					    prm->fps, size, true);
		if (err)
			goto out;

		if (ctx)
			*ctx = (struct media_ctx *)st->shared;
	}

	if (st->shared->vid.idx < 0 || !st->shared->vid.ctx) {
		info("avformat: video: media file has no video stream\n");
		err = ENOENT;
		goto out;
	}

	avformat_shared_set_video(st->shared, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


void avformat_video_copy(struct shared *st, AVPacket *pkt)
{
	struct vidpacket vp;
	AVRational tb;

	if (!st || !pkt)
		return;

	tb = st->vid.time_base;

	vp.buf = pkt->data;
	vp.size = pkt->size;
	vp.timestamp = pkt->pts * VIDEO_TIMEBASE * tb.num / tb.den;

	lock_read_get(st->lock);

	if (st->vidsrc_st && st->vidsrc_st->packeth) {
		st->vidsrc_st->packeth(&vp, st->vidsrc_st->arg);
	}

	lock_rel(st->lock);
}


void avformat_video_decode(struct shared *st, AVPacket *pkt)
{
	AVRational tb;
	struct vidframe vf;
	AVFrame *frame = 0;
	uint64_t timestamp;
	unsigned i;
	int ret;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 37, 100)
	int got_pict;
#endif

	if (!st || !st->vid.ctx)
		return;

	tb = st->vid.time_base;

	frame = av_frame_alloc();
	if (!frame)
		return;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

	ret = avcodec_send_packet(st->vid.ctx, pkt);
	if (ret < 0)
		goto out;

	ret = avcodec_receive_frame(st->vid.ctx, frame);
	if (ret < 0)
		goto out;

#else
	ret = avcodec_decode_video2(st->vid.ctx, frame, &got_pict, pkt);
	if (ret < 0 || !got_pict)
		goto out;
#endif

#if LIBAVUTIL_VERSION_MAJOR >= 56
	if (st->vid.ctx->hw_device_ctx) {
		AVFrame *frame2;
		frame2 = av_frame_alloc();
		if (!frame2)
			goto out;

		/* Many hw decoders are happy about YUV420P */
		frame2->format = AV_PIX_FMT_YUV420P;
		ret = av_hwframe_transfer_data(frame2, frame, 0);
		if (ret < 0) {
			av_frame_free(&frame2);
			goto out;
		}

		ret = av_frame_copy_props(frame2, frame);
		if (ret < 0) {
			av_frame_free(&frame2);
			goto out;
		}

		av_frame_unref(frame);
		av_frame_move_ref(frame, frame2);
		av_frame_free(&frame2);
	}
#endif

	vf.fmt = avpixfmt_to_vidfmt(frame->format);
	if (vf.fmt == (enum vidfmt)-1) {
		warning("avformat: decode: bad pixel format"
			" (%i) (%s)\n",
			frame->format,
			av_get_pix_fmt_name(frame->format));
		goto out;
	}

	vf.size.w = st->vid.ctx->width;
	vf.size.h = st->vid.ctx->height;

	for (i=0; i<4; i++) {
		vf.data[i]     = frame->data[i];
		vf.linesize[i] = frame->linesize[i];
	}

	/* convert timestamp */
	timestamp = frame->pts * VIDEO_TIMEBASE * tb.num / tb.den;

	lock_read_get(st->lock);

	if (st->vidsrc_st && st->vidsrc_st->frameh)
		st->vidsrc_st->frameh(&vf, timestamp, st->vidsrc_st->arg);

	lock_rel(st->lock);

 out:
	if (frame)
		av_frame_free(&frame);
}
