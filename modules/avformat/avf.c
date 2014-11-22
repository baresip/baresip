/**
 * @file avf.c  libavformat video-source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _BSD_SOURCE 1
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#define FF_API_OLD_METADATA 0
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>


/* extra const-correctness added in 0.9.0 */
/* note: macports has LIBSWSCALE_VERSION_MAJOR == 1 */
/* #if LIBSWSCALE_VERSION_INT >= ((0<<16) + (9<<8) + (0)) */
#if LIBSWSCALE_VERSION_MAJOR >= 2 || LIBSWSCALE_VERSION_MINOR >= 9
#define SRCSLICE_CAST (const uint8_t **)
#else
#define SRCSLICE_CAST (uint8_t **)
#endif


/* backward compat */
#if LIBAVCODEC_VERSION_MAJOR>52 || LIBAVCODEC_VERSION_INT>=((52<<16)+(64<<8))
#define LIBAVCODEC_HAVE_AVMEDIA_TYPES 1
#endif
#ifndef LIBAVCODEC_HAVE_AVMEDIA_TYPES
#define AVMEDIA_TYPE_VIDEO CODEC_TYPE_VIDEO
#endif


#if LIBAVCODEC_VERSION_INT < ((54<<16)+(25<<8)+0)
#define AVCodecID CodecID
#define AV_CODEC_ID_NONE  CODEC_ID_NONE
#endif


struct vidsrc_st {
	struct vidsrc *vs;  /* inheritance */
	pthread_t thread;
	bool run;
	AVFormatContext *ic;
	AVCodec *codec;
	AVCodecContext *ctx;
	struct SwsContext *sws;
	struct vidsz app_sz;
	struct vidsz sz;
	vidsrc_frame_h *frameh;
	void *arg;
	int sindex;
	int fps;
};


static struct vidsrc *mod_avf;


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->sws)
		sws_freeContext(st->sws);

	if (st->ctx && st->ctx->codec)
		avcodec_close(st->ctx);

	if (st->ic) {
#if LIBAVFORMAT_VERSION_INT >= ((53<<16) + (21<<8) + 0)
		avformat_close_input(&st->ic);
#else
		av_close_input_file(st->ic);
#endif
	}

	mem_deref(st->vs);
}


static void handle_packet(struct vidsrc_st *st, AVPacket *pkt)
{
	AVPicture pict;
	struct vidframe vf;
	struct vidsz sz;
	unsigned i;

	if (st->codec) {
		AVFrame frame;
		int got_pict, ret;

#if LIBAVCODEC_VERSION_INT <= ((52<<16)+(23<<8)+0)
		ret = avcodec_decode_video(st->ctx, &frame, &got_pict,
					   pkt->data, pkt->size);
#else
		ret = avcodec_decode_video2(st->ctx, &frame,
					    &got_pict, pkt);
#endif
		if (ret < 0 || !got_pict)
			return;

		sz.w = st->ctx->width;
		sz.h = st->ctx->height;

		/* check if size changed */
		if (!vidsz_cmp(&sz, &st->sz)) {
			info("size changed: %d x %d  ---> %d x %d\n",
			     st->sz.w, st->sz.h, sz.w, sz.h);
			st->sz = sz;

			if (st->sws) {
				sws_freeContext(st->sws);
				st->sws = NULL;
			}
		}

		if (!st->sws) {
			info("scaling: %d x %d  --->  %d x %d\n",
			     st->sz.w, st->sz.h,
			     st->app_sz.w, st->app_sz.h);

			st->sws = sws_getContext(st->sz.w, st->sz.h,
						 st->ctx->pix_fmt,
						 st->app_sz.w, st->app_sz.h,
						 PIX_FMT_YUV420P,
						 SWS_BICUBIC,
						 NULL, NULL, NULL);
			if (!st->sws)
				return;
		}

		ret = avpicture_alloc(&pict, PIX_FMT_YUV420P,
				      st->app_sz.w, st->app_sz.h);
		if (ret < 0)
			return;

		ret = sws_scale(st->sws,
				SRCSLICE_CAST frame.data, frame.linesize,
				0, st->sz.h, pict.data, pict.linesize);
		if (ret <= 0)
			goto end;
	}
	else {
		avpicture_fill(&pict, pkt->data, PIX_FMT_YUV420P,
			       st->sz.w, st->sz.h);
	}

	vf.size = st->app_sz;
	vf.fmt  = VID_FMT_YUV420P;
	for (i=0; i<4; i++) {
		vf.data[i]     = pict.data[i];
		vf.linesize[i] = pict.linesize[i];
	}

	st->frameh(&vf, st->arg);

 end:
	if (st->codec)
		avpicture_free(&pict);
}


static void *read_thread(void *data)
{
	struct vidsrc_st *st = data;

	while (st->run) {
		AVPacket pkt;

		av_init_packet(&pkt);

		if (av_read_frame(st->ic, &pkt) < 0) {
			sys_msleep(1000);
			av_seek_frame(st->ic, -1, 0, 0);
			continue;
		}

		if (pkt.stream_index != st->sindex)
			goto out;

		handle_packet(st, &pkt);

		/* simulate framerate */
		sys_msleep(1000/st->fps);

	out:
		av_free_packet(&pkt);
	}

	return NULL;
}


static int alloc(struct vidsrc_st **stp, struct vidsrc *vs,
		 struct media_ctx **mctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
#if LIBAVFORMAT_VERSION_INT < ((52<<16) + (110<<8) + 0)
	AVFormatParameters prms;
#endif
	struct vidsrc_st *st;
	bool found_stream = false;
	uint32_t i;
	int ret, err = 0;

	(void)mctx;
	(void)errorh;

	if (!stp || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = mem_ref(vs);
	st->app_sz = *size;
	st->frameh = frameh;
	st->arg    = arg;

	if (prm) {
		st->fps = prm->fps;
	}
	else {
		st->fps = 25;
	}

	/*
	 * avformat_open_input() was added in lavf 53.2.0 according to
	 * ffmpeg/doc/APIchanges
	 */

#if LIBAVFORMAT_VERSION_INT >= ((52<<16) + (110<<8) + 0)
	(void)fmt;
	ret = avformat_open_input(&st->ic, dev, NULL, NULL);
#else

	/* Params */
	memset(&prms, 0, sizeof(prms));

	prms.time_base          = (AVRational){1, st->fps};
	prms.channels           = 1;
	prms.width              = size->w;
	prms.height             = size->h;
	prms.pix_fmt            = PIX_FMT_YUV420P;
	prms.channel            = 0;

	ret = av_open_input_file(&st->ic, dev, av_find_input_format(fmt),
				 0, &prms);
#endif

	if (ret < 0) {
		err = ENOENT;
		goto out;
	}

#if LIBAVFORMAT_VERSION_INT >= ((53<<16) + (4<<8) + 0)
	ret = avformat_find_stream_info(st->ic, NULL);
#else
	ret = av_find_stream_info(st->ic);
#endif

	if (ret < 0) {
		warning("avformat: %s: no stream info\n", dev);
		err = ENOENT;
		goto out;
	}

#if 0
	dump_format(st->ic, 0, dev, 0);
#endif

	for (i=0; i<st->ic->nb_streams; i++) {
		const struct AVStream *strm = st->ic->streams[i];
		AVCodecContext *ctx = strm->codec;

		if (ctx->codec_type != AVMEDIA_TYPE_VIDEO)
			continue;

		debug("avformat: stream %u:  %u x %u "
		      "  time_base=%d/%d\n",
		      i, ctx->width, ctx->height,
		      ctx->time_base.num, ctx->time_base.den);

		st->sz.w   = ctx->width;
		st->sz.h   = ctx->height;
		st->ctx    = ctx;
		st->sindex = strm->index;

		if (ctx->codec_id != AV_CODEC_ID_NONE) {

			st->codec = avcodec_find_decoder(ctx->codec_id);
			if (!st->codec) {
				err = ENOENT;
				goto out;
			}

#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(8<<8)+0)
			ret = avcodec_open2(ctx, st->codec, NULL);
#else
			ret = avcodec_open(ctx, st->codec);
#endif
			if (ret < 0) {
				err = ENOENT;
				goto out;
			}
		}

		found_stream = true;
		break;
	}

	if (!found_stream) {
		err = ENOENT;
		goto out;
	}

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int module_init(void)
{
	/* register all codecs, demux and protocols */
	avcodec_register_all();
	avdevice_register_all();
	av_register_all();

	return vidsrc_register(&mod_avf, "avformat", alloc, NULL);
}


static int module_close(void)
{
	mod_avf = mem_deref(mod_avf);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avformat) = {
	"avformat",
	"vidsrc",
	module_init,
	module_close
};
