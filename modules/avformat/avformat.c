/**
 * @file avformat.c  libavformat video-source
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
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
#include <libavutil/pixdesc.h>


/**
 * @defgroup avformat avformat
 *
 * Video source using FFmpeg/libav libavformat
 *
 *
 * Example config:
 \verbatim
  video_source            avformat,/tmp/testfile.mp4
 \endverbatim
 */


#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54, 25, 0)
#define AVCodecID CodecID
#define AV_CODEC_ID_NONE  CODEC_ID_NONE
#endif


#if LIBAVUTIL_VERSION_MAJOR < 52
#define AV_PIX_FMT_YUV420P PIX_FMT_YUV420P
#define AV_PIX_FMT_YUVJ420P PIX_FMT_YUVJ420P
#endif


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */
	pthread_t thread;
	bool run;
	AVFormatContext *ic;
	AVCodec *codec;
	AVCodecContext *ctx;
	AVRational time_base;
	struct vidsz sz;
	vidsrc_frame_h *frameh;
	void *arg;
	int sindex;
};


static struct vidsrc *mod_avf;


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->ctx && st->ctx->codec)
		avcodec_close(st->ctx);

	if (st->ic) {
		avformat_close_input(&st->ic);
	}
}


static void handle_packet(struct vidsrc_st *st, AVPacket *pkt)
{
	AVFrame *frame = NULL;
	struct vidframe vf;
	struct vidsz sz;
	unsigned i;
	int64_t pts;
	uint64_t timestamp;
	const AVRational time_base = st->time_base;

	if (st->codec) {
		int got_pict, ret;

#if LIBAVUTIL_VERSION_INT >= ((52<<16)+(20<<8)+100)
		frame = av_frame_alloc();
#else
		frame = avcodec_alloc_frame();
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

		ret = avcodec_send_packet(st->ctx, pkt);
		if (ret < 0)
			goto out;

		ret = avcodec_receive_frame(st->ctx, frame);
		if (ret < 0)
			goto out;

		got_pict = true;
#else
		ret = avcodec_decode_video2(st->ctx, frame,
					    &got_pict, pkt);
#endif
		if (ret < 0 || !got_pict)
			goto out;

		sz.w = st->ctx->width;
		sz.h = st->ctx->height;

		/* check if size changed */
		if (!vidsz_cmp(&sz, &st->sz)) {
			info("avformat: size changed: %d x %d  ---> %d x %d\n",
			     st->sz.w, st->sz.h, sz.w, sz.h);
			st->sz = sz;
		}
	}
	else {
		/* No-codec option is not supported */
		return;
	}

	pts = frame->pts;

	/* convert timestamp */
	timestamp = pts * VIDEO_TIMEBASE * time_base.num / time_base.den;

	switch (frame->format) {

	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
		vf.fmt = VID_FMT_YUV420P;
		break;

	default:
		warning("avformat: decode: bad pixel format"
			" (%i) (%s)\n",
			frame->format,
			av_get_pix_fmt_name(frame->format));
		goto out;
	}

	vf.size = sz;
	for (i=0; i<4; i++) {
		vf.data[i]     = frame->data[i];
		vf.linesize[i] = frame->linesize[i];
	}

	st->frameh(&vf, timestamp, st->arg);

 out:
	if (frame) {
#if LIBAVUTIL_VERSION_INT >= ((52<<16)+(20<<8)+100)
		av_frame_free(&frame);
#else
		av_free(frame);
#endif
	}
}


static void *read_thread(void *data)
{
	struct vidsrc_st *st = data;

	uint64_t now, ts = tmr_jiffies();

	while (st->run) {
		AVPacket pkt;
		int ret;

		sys_msleep(4);
		now = tmr_jiffies();

		if (ts > now)
			continue;

		av_init_packet(&pkt);

		ret = av_read_frame(st->ic, &pkt);
		if (ret < 0) {
			debug("avformat: rewind stream (ret=%d)\n", ret);
			sys_msleep(1000);
			av_seek_frame(st->ic, -1, 0, 0);
			continue;
		}

		if (pkt.stream_index != st->sindex)
			goto out;

		handle_packet(st, &pkt);

		ts += (uint64_t) 1000 * pkt.duration * av_q2d(st->time_base);

	out:
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 12, 100)
		av_packet_unref(&pkt);
#else
		av_free_packet(&pkt);
#endif
	}

	return NULL;
}


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct media_ctx **mctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	bool found_stream = false;
	uint32_t i;
	int ret, err = 0;
	double input_fps = 0;

	(void)mctx;
	(void)fmt;
	(void)errorh;

	if (!stp || !vs || !prm || !size || !frameh)
		return EINVAL;

	debug("avformat: alloc dev='%s'\n", dev);

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->sz     = *size;
	st->frameh = frameh;
	st->arg    = arg;

	/*
	 * avformat_open_input() was added in lavf 53.2.0 according to
	 * ffmpeg/doc/APIchanges
	 */

	ret = avformat_open_input(&st->ic, dev, NULL, NULL);
	if (ret < 0) {
		warning("avformat: avformat_open_input(%s) failed (ret=%d)\n",
			dev, ret);
		err = ENOENT;
		goto out;
	}

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(53, 4, 0)
	ret = avformat_find_stream_info(st->ic, NULL);
#else
	ret = av_find_stream_info(st->ic);
#endif

	if (ret < 0) {
		warning("avformat: %s: no stream info\n", dev);
		err = ENOENT;
		goto out;
	}

	for (i=0; i<st->ic->nb_streams; i++) {
		const struct AVStream *strm = st->ic->streams[i];
		AVCodecContext *ctx;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(57, 33, 100)

		ctx = avcodec_alloc_context3(NULL);
		if (!ctx) {
			err = ENOMEM;
			goto out;
		}

		ret = avcodec_parameters_to_context(ctx, strm->codecpar);
		if (ret < 0) {
			warning("avformat: avcodec_parameters_to_context\n");
			err = EPROTO;
			goto out;
		}

#else
		ctx = strm->codec;
#endif

		if (ctx->codec_type != AVMEDIA_TYPE_VIDEO)
			continue;

		debug("avformat: stream %u:  %u x %u "
		      "  time_base=%d/%d\n",
		      i, ctx->width, ctx->height,
		      strm->time_base.num, strm->time_base.den);

		st->sz.w   = ctx->width;
		st->sz.h   = ctx->height;
		st->ctx    = ctx;
		st->sindex = strm->index;
		st->time_base = strm->time_base;

		input_fps = av_q2d(strm->avg_frame_rate);
		if (prm->fps != input_fps) {
			info("avformat: updating %.2f fps from config"
			     " to native "
			     "input material fps %.2f\n",
			     prm->fps, input_fps);

			prm->fps = input_fps;
		}

		if (ctx->codec_id != AV_CODEC_ID_NONE) {

			st->codec = avcodec_find_decoder(ctx->codec_id);
			if (!st->codec) {
				err = ENOENT;
				goto out;
			}

			ret = avcodec_open2(ctx, st->codec, NULL);
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
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif
	avdevice_register_all();

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(53, 13, 0)
	avformat_network_init();
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	av_register_all();
#endif

	return vidsrc_register(&mod_avf, baresip_vidsrcl(),
			       "avformat", alloc, NULL);
}


static int module_close(void)
{
	mod_avf = mem_deref(mod_avf);

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(53, 13, 0)
	avformat_network_deinit();
#endif

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avformat) = {
	"avformat",
	"vidsrc",
	module_init,
	module_close
};
