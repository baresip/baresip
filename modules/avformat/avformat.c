/**
 * @file avformat.c  libavformat media-source
 *
 * Copyright (C) 2010 - 2020 Alfred E. Heggestad
 * Copyright (C) 2021 by:
 *     Media Magic Technologies <developer@mediamagictechnologies.com>
 *     and Divus GmbH <developer@divus.eu>
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <re_atomic.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/hwcontext.h>
#include "mod_avformat.h"


/**
 * @defgroup avformat avformat
 *
 * Audio/video source using FFmpeg libavformat
 *
 *
 * Example config:
 \verbatim
  audio_source            avformat,/tmp/testfile.mp4
  video_source            avformat,/tmp/testfile.mp4

  avformat_hwaccel		vaapi
  avformat_inputformat	mjpeg
 \endverbatim
 */


static struct ausrc *ausrc;
static struct vidsrc *mod_avf;

static enum AVHWDeviceType avformat_hwdevice = AV_HWDEVICE_TYPE_NONE;
static char avformat_inputformat[64];
static const AVCodec *avformat_decoder;
static char pass_through[256] = "";
static char rtsp_transport[256] = "";


static struct list sharedl;


static void shared_destructor(void *arg)
{
	struct shared *st = arg;

	if (re_atomic_rlx(&st->run)) {
		debug("avformat: stopping read thread\n");
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	if (st->au.ctx) {
		avcodec_free_context(&st->au.ctx);
	}

	if (st->vid.ctx) {
		avcodec_free_context(&st->vid.ctx);
	}

	if (st->ic)
		avformat_close_input(&st->ic);

	list_unlink(&st->le);
	mtx_destroy(&st->lock);
	mem_deref(st->dev);
}


static int read_thread(void *data)
{
	struct shared *st = data;
	uint64_t now, offset = tmr_jiffies();
	double auts = 0, vidts = 0;
	AVPacket *pkt;

	pkt = av_packet_alloc();
	if (!pkt)
		return ENOMEM;

	while (re_atomic_rlx(&st->run)) {

		int ret;

		sys_msleep(4);

		now = tmr_jiffies();

		for (;;) {
			double xts;

			if (!re_atomic_rlx(&st->run))
				break;

			if (st->au.idx >=0 && st->vid.idx >=0)
				xts = min(auts, vidts);
			else if (st->au.idx >=0)
				xts = auts;
			else if (st->vid.idx >=0)
				xts = vidts;
			else
				break;

			if (!(st->is_realtime))
				if (now < (offset + xts))
					break;

			ret = av_read_frame(st->ic, pkt);
			if (ret == (int)AVERROR_EOF) {

				debug("avformat: rewind stream\n");

				sys_msleep(1000);

				ret = av_seek_frame(st->ic, -1, 0,
						    AVSEEK_FLAG_BACKWARD);
				if (ret < 0) {
					info("avformat: seek error (%d)\n",
					     ret);
					goto out;
				}

				offset = tmr_jiffies();
				auts = vidts = 0;
				break;
			}
			else if (ret < 0) {
				debug("avformat: read error (%d)\n", ret);
				goto out;
			}

			if (pkt->stream_index == st->au.idx) {

				if (pkt->pts == AV_NOPTS_VALUE) {
					warning("no audio pts\n");
				}

				auts = 1000 * pkt->pts *
					av_q2d(st->au.time_base);

				avformat_audio_decode(st, pkt);
			}
			else if (pkt->stream_index == st->vid.idx) {

				if (pkt->pts == AV_NOPTS_VALUE) {
					warning("no video pts\n");
				}

				vidts = 1000 * pkt->pts *
					av_q2d(st->vid.time_base);

				if (st->is_pass_through) {
					avformat_video_copy(st, pkt);
				}
				else {
					avformat_video_decode(st, pkt);
				}
			}

			av_packet_unref(pkt);
		}
	}

 out:
	av_packet_free(&pkt);

	return 0;
}


static int open_codec(struct stream *s, const struct AVStream *strm, int i,
		      AVCodecContext *ctx, bool use_codec)
{
	const AVCodec *codec = avformat_decoder;
	int ret;

	if (s->idx >= 0 || s->ctx)
		return 0;

	if (!codec && use_codec) {
		codec = avcodec_find_decoder(ctx->codec_id);
		if (!codec) {
			info("avformat: can't find codec %i\n", ctx->codec_id);
			return ENOENT;
		}
	}

	if (use_codec) {

		ret = avcodec_open2(ctx, codec, NULL);
		if (ret < 0) {
			warning("avformat: error opening codec (%i)\n", ret);
			return ENOMEM;
		}
	}

	if (avformat_hwdevice != AV_HWDEVICE_TYPE_NONE) {
		AVBufferRef *hwctx;

		ret = av_hwdevice_ctx_create(&hwctx, avformat_hwdevice,
					     NULL, NULL, 0);
		if (ret < 0) {
			warning("avformat: error opening hw device '%s'"
				" (%i) (%s)\n",
			        av_hwdevice_get_type_name(avformat_hwdevice),
				ret, av_err2str(ret));
			return ENOMEM;
		}

		ctx->hw_device_ctx = av_buffer_ref(hwctx);

		av_buffer_unref(&hwctx);
	}

	s->time_base = strm->time_base;
	s->ctx = ctx;
	s->idx = i;

	if (use_codec) {
		debug("avformat: '%s' using decoder '%s' (%s)\n",
		      av_get_media_type_string(ctx->codec_type),
		      codec->name, codec->long_name);
	}
	else {
		debug("avformat: '%s' using pass-through\n",
		      av_get_media_type_string(ctx->codec_type));
	}

	return 0;
}


int avformat_shared_alloc(struct shared **shp, const char *dev,
			  double fps, const struct vidsz *size,
			  bool video)
{
	struct shared *st;
	struct pl pl_fmt, pl_dev;
	char *device = NULL;
#if LIBAVUTIL_VERSION_MAJOR >= 57
	const AVInputFormat *input_format = NULL;
#else
	AVInputFormat *input_format = NULL;
#endif
	AVDictionary *format_opts = NULL;
	char buf[16];
	unsigned i;
	int err;
	int ret;

	if (!shp || !dev)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), shared_destructor);
	if (!st)
		return ENOMEM;

	st->au.idx  = -1;
	st->vid.idx = -1;

	err = str_dup(&st->dev, dev);
	if (err)
		goto out;

	conf_get_str(conf_cur(), "avformat_pass_through",
			  pass_through, sizeof(pass_through));

	if (*pass_through != '\0' && 0==strcmp(pass_through, "yes")) {
		st->is_pass_through = true;
	}

	if (0 == re_regex(dev, str_len(dev), "[^,]+,[^]+", &pl_fmt, &pl_dev)) {

		char format[32];

		pl_strcpy(&pl_fmt, format, sizeof(format));

		pl_strdup(&device, &pl_dev);
		dev = device;

		st->is_realtime =
			0==strcmp(format, "avfoundation") ||
			0==strcmp(format, "android_camera") ||
			0==strcmp(format, "v4l2");

		input_format = av_find_input_format(format);
		if (input_format) {
			debug("avformat: using format '%s' (%s)\n",
			      input_format->name, input_format->long_name);
		}
		else {
			warning("avformat: input format not found (%s)\n",
				format);
		}
	}

	err = mtx_init(&st->lock, mtx_plain) != thrd_success;
	if (err) {
		err = ENOMEM;
		goto out;
	}

	if (video && size->w) {
		re_snprintf(buf, sizeof(buf), "%ux%u", size->w, size->h);
		ret = av_dict_set(&format_opts, "video_size", buf, 0);
		if (ret != 0) {
			warning("avformat: av_dict_set(video_size) failed"
				" (ret=%s)\n", av_err2str(ret));
			err = ENOENT;
			goto out;
		}
	}

	if (video && fps && !st->is_pass_through) {
		re_snprintf(buf, sizeof(buf), "%2.f", fps);
		ret = av_dict_set(&format_opts, "framerate", buf, 0);
		if (ret != 0) {
			warning("avformat: av_dict_set(framerate) failed"
				" (ret=%s)\n", av_err2str(ret));
			err = ENOENT;
			goto out;
		}
	}

	if (video && device) {
		ret = av_dict_set(&format_opts, "camera_index", device, 0);
		if (ret != 0) {
			warning("avformat: av_dict_set(camera_index) failed"
				" (ret=%s)\n", av_err2str(ret));
			err = ENOENT;
			goto out;
		}
	}

	if (str_isset(avformat_inputformat)) {
		ret = av_dict_set(&format_opts, "input_format",
                                avformat_inputformat, 0);
		if (ret != 0) {
			warning("avformat: av_dict_set(input_format) failed"
					" (ret=%s)\n", av_err2str(ret));
			err = ENOENT;
			goto out;
		}
	}

	if (str_isset(rtsp_transport)) {
		ret = -1;

		if ((0==strcmp(rtsp_transport, "tcp")) ||
		    (0==strcmp(rtsp_transport, "udp")) ||
		    (0==strcmp(rtsp_transport, "udp_multicast")) ||
		    (0==strcmp(rtsp_transport, "http")) ||
		    (0==strcmp(rtsp_transport, "https"))) {

			ret = av_dict_set(&format_opts, "rtsp_transport",
					  rtsp_transport, 0);
		}

		if (ret != 0) {
			warning("avformat: av_dict_set(rtsp_transport) failed"
				" (ret=%s)\n", av_err2str(ret));
			err = ENOENT;
			goto out;
		}
	}

	ret = avformat_open_input(&st->ic, dev, input_format, &format_opts);
	if (ret < 0) {
		warning("avformat: avformat_open_input(%s) failed (ret=%s)\n",
			dev, av_err2str(ret));
		err = ENOENT;
		goto out;
	}

	for (i=0; i<st->ic->nb_streams; i++) {

		const struct AVStream *strm = st->ic->streams[i];
		AVCodecContext *ctx;

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

		switch (ctx->codec_type) {

		case AVMEDIA_TYPE_AUDIO:
			err = open_codec(&st->au, strm, i, ctx, true);
			if (err)
				goto out;
			break;

		case AVMEDIA_TYPE_VIDEO:
			err = open_codec(&st->vid, strm, i, ctx,
					 !st->is_pass_through);
			if (err)
				goto out;
			break;

		default:
			break;
		}
	}

	re_atomic_rlx_set(&st->run, true);
	err = thread_create_name(&st->thread, "avformat", read_thread, st);
	if (err) {
		re_atomic_rlx_set(&st->run, false);
		goto out;
	}

	list_append(&sharedl, &st->le, st);

 out:

	if (err)
		mem_deref(st);
	else
		*shp = st;

	mem_deref(device);

	av_dict_free(&format_opts);

	return err;
}


struct shared *avformat_shared_lookup(const char *dev)
{
	struct le *le;

	for (le = sharedl.head; le; le = le->next) {

		struct shared *sh = le->data;
		bool have_av = sh->au.ctx != NULL && sh->vid.ctx != NULL;

		if (have_av && 0 == str_casecmp(sh->dev, dev))
			return sh;
	}

	return NULL;
}


void avformat_shared_set_audio(struct shared *sh, struct ausrc_st *st)
{
	if (!sh)
		return;

	mtx_lock(&sh->lock);
	sh->ausrc_st = st;
	mtx_unlock(&sh->lock);
}


void avformat_shared_set_video(struct shared *sh, struct vidsrc_st *st)
{
	if (!sh)
		return;

	mtx_lock(&sh->lock);
	sh->vidsrc_st = st;
	mtx_unlock(&sh->lock);
}


static int module_init(void)
{
	int err;
	char hwaccel[64] = "";
	char decoder[64] = "";

	conf_get_str(conf_cur(), "avformat_hwaccel", hwaccel, sizeof(hwaccel));
	if (str_isset(hwaccel)) {
		avformat_hwdevice = av_hwdevice_find_type_by_name(hwaccel);
		if (avformat_hwdevice == AV_HWDEVICE_TYPE_NONE) {
			warning("avformat: hwdevice not found (%s)\n",
                                        hwaccel);
		}
	}

	conf_get_str(conf_cur(), "avformat_inputformat", avformat_inputformat,
			sizeof(avformat_inputformat));

	conf_get_str(conf_cur(), "avformat_decoder", decoder,
			sizeof(decoder));

	conf_get_str(conf_cur(), "avformat_rtsp_transport",
		     rtsp_transport, sizeof(rtsp_transport));

	if (str_isset(decoder)) {
		avformat_decoder = avcodec_find_decoder_by_name(decoder);
		if (!avformat_decoder) {
			warning("avformat: decoder not found (%s)\n", decoder);
			return ENOENT;
		}
	}

	avformat_network_init();

	avdevice_register_all();

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "avformat", avformat_audio_alloc);

	err |= vidsrc_register(&mod_avf, baresip_vidsrcl(),
			       "avformat", avformat_video_alloc, NULL);

	return err;
}


static int module_close(void)
{
	mod_avf = mem_deref(mod_avf);
	ausrc = mem_deref(ausrc);

	avformat_network_deinit();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avformat) = {
	"avformat",
	"avsrc",
	module_init,
	module_close
};
