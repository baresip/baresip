/**
 * @file avcodec/encode.c  Video codecs using libavcodec -- encoder
 *
 * Copyright (C) 2010 - 2013 Alfred E. Heggestad
 * Copyright (C) 2021 by:
 *     Media Magic Technologies <developer@mediamagictechnologies.com>
 *     and Divus GmbH <developer@divus.eu>
 */
#include <re.h>
#include <re_h265.h>
#include <rem.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include "h26x.h"
#include "avcodec.h"


enum {
	KEYFRAME_INTERVAL = 10  /* Keyframes per second */
};


struct videnc_state {
	const AVCodec *codec;
	AVCodecContext *ctx;
	struct mbuf *mb_frag;
	struct videnc_param encprm;
	struct vidsz encsize;
	enum vidfmt fmt;
	enum AVCodecID codec_id;
	videnc_packet_h *pkth;
	const struct video *vid;

	union {
		struct {
			uint32_t packetization_mode;
			uint32_t profile_idc;
			uint32_t profile_iop;
			uint32_t level_idc;
			uint32_t max_fs;
			uint32_t max_smbps;
		} h264;
	} u;
};


static void destructor(void *arg)
{
	struct videnc_state *st = arg;

	mem_deref(st->mb_frag);

	if (st->ctx)
		avcodec_free_context(&st->ctx);
}


static int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *device_ctx,
			   int width, int height)
{
	AVBufferRef *hw_frames_ref;
	AVHWFramesContext *frames_ctx = NULL;
	int err = 0;

	info("avcodec: encode: create hardware frames.. (%d x %d)\n",
	     width, height);

	if (!(hw_frames_ref = av_hwframe_ctx_alloc(device_ctx))) {
		warning("avcodec: encode: Failed to create hardware"
			" frame context.\n");
		return ENOMEM;
	}

	frames_ctx = (AVHWFramesContext *)(void *)hw_frames_ref->data;
	frames_ctx->format    = avcodec_hw_pix_fmt;
	frames_ctx->sw_format = AV_PIX_FMT_NV12;
	frames_ctx->width     = width;
	frames_ctx->height    = height;
	frames_ctx->initial_pool_size = 20;

	if ((err = av_hwframe_ctx_init(hw_frames_ref)) < 0) {
		warning("avcodec: encode:"
			" Failed to initialize hardware frame context."
			"Error code: %s\n",av_err2str(err));
		av_buffer_unref(&hw_frames_ref);
		return err;
	}

	ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
	if (!ctx->hw_frames_ctx)
		err = AVERROR(ENOMEM);

	av_buffer_unref(&hw_frames_ref);

	return err;
}


static enum AVPixelFormat vidfmt_to_avpixfmt(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P: return AV_PIX_FMT_YUV420P;
	case VID_FMT_YUV444P: return AV_PIX_FMT_YUV444P;
	case VID_FMT_NV12:    return AV_PIX_FMT_NV12;
	case VID_FMT_NV21:    return AV_PIX_FMT_NV21;
	case VID_FMT_YUV422P: return AV_PIX_FMT_YUV422P;
	default:              return AV_PIX_FMT_NONE;
	}
}


static int init_encoder(struct videnc_state *st, const char *name)
{
	/*
	 * Special handling of H.264 encoder
	 */
	if (st->codec_id == AV_CODEC_ID_H264 && avcodec_h264enc) {

		st->codec = avcodec_h264enc;

		info("avcodec: h264 encoder activated\n");

		return 0;
	}

	if (0 == str_casecmp(name, "h265")) {

		st->codec = avcodec_h265enc;

		info("avcodec: h265 encoder activated\n");

		return 0;
	}

	st->codec = avcodec_find_encoder(st->codec_id);
	if (!st->codec)
		return ENOENT;

	return 0;
}


static int open_encoder(struct videnc_state *st,
			const struct videnc_param *prm,
			const struct vidsz *size,
			int pix_fmt)
{
	int err = 0;
	uint32_t keyint = KEYFRAME_INTERVAL;

	if (st->ctx)
		avcodec_free_context(&st->ctx);

	st->ctx = avcodec_alloc_context3(st->codec);
	if (!st->ctx) {
		err = ENOMEM;
		goto out;
	}

	av_opt_set_defaults(st->ctx);

	st->ctx->bit_rate	= prm->bitrate;
	st->ctx->rc_max_rate	= prm->bitrate;
	st->ctx->rc_buffer_size = prm->bitrate / 2;

	st->ctx->width     = size->w;
	st->ctx->height    = size->h;

	if (avcodec_hw_type == AV_HWDEVICE_TYPE_VAAPI)
		st->ctx->pix_fmt   = avcodec_hw_pix_fmt;
	else
		st->ctx->pix_fmt   = pix_fmt;

	conf_get_u32(conf_cur(), "avcodec_keyint", &keyint);

	st->ctx->time_base.num = 1;
	st->ctx->time_base.den = prm->fps;
	st->ctx->gop_size = keyint * prm->fps;

	if (0 == str_cmp(st->codec->name, "libx264")) {

		av_opt_set(st->ctx->priv_data, "profile", "baseline", 0);
		av_opt_set(st->ctx->priv_data, "preset", "ultrafast", 0);
		av_opt_set(st->ctx->priv_data, "tune", "zerolatency", 0);

		if (st->u.h264.packetization_mode == 0) {
			av_opt_set_int(st->ctx->priv_data,
				       "slice-max-size", prm->pktsize, 0);
		}
	}

	/* params to avoid libavcodec/x264 default preset error */
	if (st->codec_id == AV_CODEC_ID_H264) {

		if (0 == str_cmp(st->codec->name, "h264_vaapi")) {
			av_opt_set(st->ctx->priv_data, "profile",
				   "constrained_baseline", 0);
		}
		else {
			av_opt_set(st->ctx->priv_data, "profile",
				   "baseline", 0);
		}

		st->ctx->me_range = 16;
		st->ctx->qmin = 10;
		st->ctx->qmax = 51;
		st->ctx->max_qdiff = 4;

		if (st->codec == avcodec_find_encoder_by_name("nvenc_h264") ||
		    st->codec == avcodec_find_encoder_by_name("h264_nvenc")) {

			err = av_opt_set(st->ctx->priv_data,
					 "preset", "llhp", 0);
			if (err < 0) {
				debug("avcodec: h264 nvenc setting preset "
				      "\"llhp\" failed; error: %u\n", err);
			}
			else {
				debug("avcodec: h264 nvenc preset "
				      "\"llhp\" selected\n");
			}
			err = av_opt_set_int(st->ctx->priv_data,
					     "2pass", 1, 0);
			if (err < 0) {
				debug("avcodec: h264 nvenc option "
				      "\"2pass\" failed; error: %u\n", err);
			}
			else {
				debug("avcodec: h264 nvenc option "
				      "\"2pass\" selected\n");
			}
		}
	}

	if (0 == str_cmp(st->codec->name, "libx265")) {

		av_opt_set(st->ctx->priv_data, "profile", "main444-8", 0);
		av_opt_set(st->ctx->priv_data, "preset", "ultrafast", 0);
		av_opt_set(st->ctx->priv_data, "tune", "zerolatency", 0);
	}

	if (avcodec_hw_type == AV_HWDEVICE_TYPE_VAAPI) {

		/* set hw_frames_ctx for encoder's AVCodecContext */

		err = set_hwframe_ctx(st->ctx, avcodec_hw_device_ctx,
				      size->w, size->h);
		if (err < 0) {

			warning("avcodec: encode: Failed to set"
				" hwframe context.\n");
			goto out;
		}
	}

	if (avcodec_open2(st->ctx, st->codec, NULL) < 0) {
		err = ENOENT;
		goto out;
	}

	st->encsize = *size;

 out:
	if (err) {
		if (st->ctx)
			avcodec_free_context(&st->ctx);
	}

	return err;
}


static int decode_sdpparam_h264(struct videnc_state *st, const struct pl *name,
				const struct pl *val)
{
	if (0 == pl_strcasecmp(name, "packetization-mode")) {
		st->u.h264.packetization_mode = pl_u32(val);

		if (st->u.h264.packetization_mode != 0 &&
		    st->u.h264.packetization_mode != 1 ) {
			warning("avcodec: illegal packetization-mode %u\n",
				st->u.h264.packetization_mode);
			return EPROTO;
		}
	}
	else if (0 == pl_strcasecmp(name, "profile-level-id")) {
		struct pl prof = *val;
		if (prof.l != 6) {
			warning("avcodec: invalid profile-level-id (%r)\n",
				val);
			return EPROTO;
		}

		prof.l = 2;
		st->u.h264.profile_idc = pl_x32(&prof); prof.p += 2;
		st->u.h264.profile_iop = pl_x32(&prof); prof.p += 2;
		st->u.h264.level_idc   = pl_x32(&prof);
	}
	else if (0 == pl_strcasecmp(name, "max-fs")) {
		st->u.h264.max_fs = pl_u32(val);
	}
	else if (0 == pl_strcasecmp(name, "max-smbps")) {
		st->u.h264.max_smbps = pl_u32(val);
	}

	return 0;
}


static void param_handler(const struct pl *name, const struct pl *val,
			  void *arg)
{
	struct videnc_state *st = arg;

	if (st->codec_id == AV_CODEC_ID_H264)
		(void)decode_sdpparam_h264(st, name, val);
}


int avcodec_encode_update(struct videnc_state **vesp,
			  const struct vidcodec *vc, struct videnc_param *prm,
			  const char *fmtp, videnc_packet_h *pkth,
			  const struct video *vid)
{
	struct videnc_state *st;
	int err = 0;

	if (!vesp || !vc || !prm || !pkth)
		return EINVAL;

	if (*vesp)
		return 0;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->encprm = *prm;
	st->pkth   = pkth;
	st->vid	   = vid;

	st->codec_id = avcodec_resolve_codecid(vc->name);
	if (st->codec_id == AV_CODEC_ID_NONE) {
		warning("avcodec: unknown encoder (%s)\n", vc->name);
		err = EINVAL;
		goto out;
	}

	st->mb_frag = mbuf_alloc(1024);
	if (!st->mb_frag) {
		err = ENOMEM;
		goto out;
	}

	st->fmt = -1;

	err = init_encoder(st, vc->name);
	if (err) {
		warning("avcodec: %s: could not init encoder\n", vc->name);
		goto out;
	}

	if (str_isset(fmtp)) {
		struct pl sdp_fmtp;

		pl_set_str(&sdp_fmtp, fmtp);

		fmt_param_apply(&sdp_fmtp, param_handler, st);
	}

	debug("avcodec: video encoder %s: %.2f fps, %d bit/s, pktsize=%u\n",
	      vc->name, prm->fps, prm->bitrate, prm->pktsize);

 out:
	if (err)
		mem_deref(st);
	else
		*vesp = st;

	return err;
}


int avcodec_encode(struct videnc_state *st, bool update,
		   const struct vidframe *frame, uint64_t timestamp)
{
	AVFrame *pict = NULL;
	AVFrame *hw_frame = NULL;
	AVPacket *pkt = NULL;
	int i, err = 0, ret;
	uint64_t ts;

	if (!st || !frame)
		return EINVAL;

	if (!st->ctx || !vidsz_cmp(&st->encsize, &frame->size) ||
	    st->fmt != frame->fmt) {

		enum AVPixelFormat pix_fmt;

		pix_fmt = vidfmt_to_avpixfmt(frame->fmt);
		if (pix_fmt == AV_PIX_FMT_NONE) {
			warning("avcodec: pixel format not supported (%s)\n",
				vidfmt_name(frame->fmt));
			return ENOTSUP;
		}

		err = open_encoder(st, &st->encprm, &frame->size, pix_fmt);
		if (err) {
			warning("avcodec: open_encoder: %m\n", err);
			return err;
		}

		st->fmt = frame->fmt;
	}

	pict = av_frame_alloc();
	if (!pict) {
		err = ENOMEM;
		goto out;
	}

	if (avcodec_hw_type == AV_HWDEVICE_TYPE_VAAPI) {
		hw_frame = av_frame_alloc();
		if (!hw_frame) {
			err = ENOMEM;
			goto out;
		}
	}

	pict->format = vidfmt_to_avpixfmt(frame->fmt);
	pict->width = frame->size.w;
	pict->height = frame->size.h;
	pict->pts = timestamp;

	for (i=0; i<4; i++) {
		pict->data[i]     = frame->data[i];
		pict->linesize[i] = frame->linesize[i];
	}

	if (update) {
		debug("avcodec: encoder picture update\n");
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)
		pict->flags |= AV_FRAME_FLAG_KEY;
#else
		pict->key_frame = 1;
#endif
		pict->pict_type = AV_PICTURE_TYPE_I;
	}

	pict->color_range = AVCOL_RANGE_MPEG;

	if (avcodec_hw_type == AV_HWDEVICE_TYPE_VAAPI) {

		if ((err = av_hwframe_get_buffer(st->ctx->hw_frames_ctx,
						 hw_frame, 0)) < 0) {
			warning("avcodec: encode: Error code: %s.\n",
				av_err2str(err));
			goto out;
		}

		if (!hw_frame->hw_frames_ctx) {
			err = AVERROR(ENOMEM);
			goto out;
		}

		if ((err = av_hwframe_transfer_data(hw_frame, pict, 0)) < 0) {
			warning("avcodec: encode: Error while transferring"
				" frame data to surface."
				"Error code: %s.\n", av_err2str(err));
			goto out;
		}

		av_frame_copy_props(hw_frame, pict);
	}

	pkt = av_packet_alloc();
	if (!pkt) {
		err = ENOMEM;
		goto out;
	}

	ret = avcodec_send_frame(st->ctx, hw_frame ? hw_frame : pict);
	if (ret < 0) {
		err = EBADMSG;
		goto out;
	}

	ret = avcodec_receive_packet(st->ctx, pkt);
	if (ret < 0) {
		err = 0;
		goto out;
	}

	ts = video_calc_rtp_timestamp_fix(pkt->pts);

	switch (st->codec_id) {

	case AV_CODEC_ID_H264:
		err = h264_packetize(
			ts, pkt->data, pkt->size, st->encprm.pktsize,
			(h264_packet_h *)st->pkth, (void *)st->vid);
		break;

	case AV_CODEC_ID_H265:
		err = h265_packetize(
			ts, pkt->data, pkt->size, st->encprm.pktsize,
			(h265_packet_h *)st->pkth, (void *)st->vid);
		break;

	default:
		err = EPROTO;
		break;
	}

 out:
	if (pict)
		av_free(pict);
	if (pkt)
		av_packet_free(&pkt);
	av_frame_free(&hw_frame);

	return err;
}


int avcodec_packetize(struct videnc_state *st, const struct vidpacket *packet)
{
	int err = 0;
	uint64_t ts;

	if (!st || !packet)
		return EINVAL;

	ts = video_calc_rtp_timestamp_fix(packet->timestamp);

	switch (st->codec_id) {

	case AV_CODEC_ID_H264:
		err = h264_packetize(
			ts, packet->buf, packet->size, st->encprm.pktsize,
			(h264_packet_h *)st->pkth, (void *)st->vid);
		break;

	case AV_CODEC_ID_H265:
		err = h265_packetize(
			ts, packet->buf, packet->size, st->encprm.pktsize,
			(h265_packet_h *)st->pkth, (void *)st->vid);
		break;

	default:
		err = EPROTO;
		break;
	}

	return err;
}
