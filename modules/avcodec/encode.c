/**
 * @file avcodec/encode.c  Video codecs using libavcodec -- encoder
 *
 * Copyright (C) 2010 - 2013 Creytiv.com
 */
#include <re.h>
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


struct picsz {
	enum h263_fmt fmt;  /**< Picture size */
	uint8_t mpi;        /**< Minimum Picture Interval (1-32) */
};


struct videnc_state {
	AVCodec *codec;
	AVCodecContext *ctx;
	struct mbuf *mb_frag;
	struct videnc_param encprm;
	struct vidsz encsize;
	enum vidfmt fmt;
	enum AVCodecID codec_id;
	videnc_packet_h *pkth;
	void *arg;

	union {
		struct {
			struct picsz picszv[8];
			uint32_t picszn;
		} h263;

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


#if LIBAVUTIL_VERSION_MAJOR >= 56
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
#endif


static enum AVPixelFormat vidfmt_to_avpixfmt(enum vidfmt fmt)
{
	switch (fmt) {

	case VID_FMT_YUV420P: return AV_PIX_FMT_YUV420P;
	case VID_FMT_YUV444P: return AV_PIX_FMT_YUV444P;
	case VID_FMT_NV12:    return AV_PIX_FMT_NV12;
	case VID_FMT_NV21:    return AV_PIX_FMT_NV21;
	default:              return AV_PIX_FMT_NONE;
	}
}


static enum h263_fmt h263_fmt(const struct pl *name)
{
	if (0 == pl_strcasecmp(name, "sqcif")) return H263_FMT_SQCIF;
	if (0 == pl_strcasecmp(name, "qcif"))  return H263_FMT_QCIF;
	if (0 == pl_strcasecmp(name, "cif"))   return H263_FMT_CIF;
	if (0 == pl_strcasecmp(name, "cif4"))  return H263_FMT_4CIF;
	if (0 == pl_strcasecmp(name, "cif16")) return H263_FMT_16CIF;
	return H263_FMT_OTHER;
}


static int decode_sdpparam_h263(struct videnc_state *st, const struct pl *name,
				const struct pl *val)
{
	enum h263_fmt fmt = h263_fmt(name);
	const int mpi = pl_u32(val);

	if (fmt == H263_FMT_OTHER) {
		info("h263: unknown param '%r'\n", name);
		return 0;
	}
	if (mpi < 1 || mpi > 32) {
		info("h263: %r: MPI out of range %d\n", name, mpi);
		return 0;
	}

	if (st->u.h263.picszn >= ARRAY_SIZE(st->u.h263.picszv)) {
		info("h263: picszv overflow: %r\n", name);
		return 0;
	}

	st->u.h263.picszv[st->u.h263.picszn].fmt = fmt;
	st->u.h263.picszv[st->u.h263.picszn].mpi = mpi;

	++st->u.h263.picszn;

	return 0;
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

	if (st->ctx)
		avcodec_free_context(&st->ctx);

	st->ctx = avcodec_alloc_context3(st->codec);
	if (!st->ctx) {
		err = ENOMEM;
		goto out;
	}

	av_opt_set_defaults(st->ctx);

	st->ctx->bit_rate  = prm->bitrate;
	st->ctx->width     = size->w;
	st->ctx->height    = size->h;

#if LIBAVUTIL_VERSION_MAJOR >= 56
	if (avcodec_hw_type == AV_HWDEVICE_TYPE_VAAPI)
		st->ctx->pix_fmt   = avcodec_hw_pix_fmt;
	else
#endif
		st->ctx->pix_fmt   = pix_fmt;

	st->ctx->time_base.num = 1;
	st->ctx->time_base.den = prm->fps;
	st->ctx->gop_size = KEYFRAME_INTERVAL * prm->fps;

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

#if LIBAVUTIL_VERSION_MAJOR >= 56
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
#endif

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

	if (st->codec_id == AV_CODEC_ID_H263)
		(void)decode_sdpparam_h263(st, name, val);
	else if (st->codec_id == AV_CODEC_ID_H264)
		(void)decode_sdpparam_h264(st, name, val);
}


static int general_packetize(uint64_t rtp_ts, struct mbuf *mb, size_t pktsize,
			     videnc_packet_h *pkth, void *arg)
{
	int err = 0;

	/* Assemble frame into smaller packets */
	while (!err) {
		size_t sz, left = mbuf_get_left(mb);
		bool last = (left < pktsize);
		if (!left)
			break;

		sz = last ? left : pktsize;

		err = pkth(last, rtp_ts, NULL, 0, mbuf_buf(mb), sz,
			   arg);

		mbuf_advance(mb, sz);
	}

	return err;
}


static int h263_packetize(struct videnc_state *st,
			  uint64_t rtp_ts, struct mbuf *mb,
			  videnc_packet_h *pkth, void *arg)
{
	struct h263_strm h263_strm;
	struct h263_hdr h263_hdr;
	size_t pos;
	int err;

	/* Decode bit-stream header, used by packetizer */
	err = h263_strm_decode(&h263_strm, mb);
	if (err)
		return err;

	h263_hdr_copy_strm(&h263_hdr, &h263_strm);

	st->mb_frag->pos = st->mb_frag->end = 0;
	err = h263_hdr_encode(&h263_hdr, st->mb_frag);
	pos = st->mb_frag->pos;

	/* Assemble frame into smaller packets */
	while (!err) {
		size_t sz, left = mbuf_get_left(mb);
		bool last = (left < st->encprm.pktsize);
		if (!left)
			break;

		sz = last ? left : st->encprm.pktsize;

		st->mb_frag->pos = st->mb_frag->end = pos;
		err = mbuf_write_mem(st->mb_frag, mbuf_buf(mb), sz);
		if (err)
			break;

		st->mb_frag->pos = 0;

		err = pkth(last, rtp_ts, NULL, 0, mbuf_buf(st->mb_frag),
			   mbuf_get_left(st->mb_frag), arg);

		mbuf_advance(mb, sz);
	}

	return err;
}


int avcodec_encode_update(struct videnc_state **vesp,
			  const struct vidcodec *vc,
			  struct videnc_param *prm, const char *fmtp,
			  videnc_packet_h *pkth, void *arg)
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
	st->pkth = pkth;
	st->arg = arg;

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
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 37, 100)
	int got_packet = 0;
#endif
	uint64_t ts;
	struct mbuf mb;

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

#if LIBAVUTIL_VERSION_MAJOR >= 56
	if (avcodec_hw_type == AV_HWDEVICE_TYPE_VAAPI) {
		hw_frame = av_frame_alloc();
		if (!hw_frame) {
			err = ENOMEM;
			goto out;
		}
	}
#endif

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
		pict->key_frame = 1;
		pict->pict_type = AV_PICTURE_TYPE_I;
	}

#if LIBAVUTIL_VERSION_MAJOR >= 55
	pict->color_range = AVCOL_RANGE_MPEG;
#endif

#if LIBAVUTIL_VERSION_MAJOR >= 56
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
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

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
#else

	pkt = av_malloc(sizeof(*pkt));
	if (!pkt) {
		err = ENOMEM;
		goto out;
	}

	av_init_packet(pkt);
	av_new_packet(pkt, 65536);

	ret = avcodec_encode_video2(st->ctx, pkt, pict, &got_packet);
	if (ret < 0) {
		err = EBADMSG;
		goto out;
	}

	if (!got_packet)
		return 0;
#endif

	mb.buf = pkt->data;
	mb.pos = 0;
	mb.end = pkt->size;
	mb.size = pkt->size;

	ts = video_calc_rtp_timestamp_fix(pkt->pts);

	switch (st->codec_id) {

	case AV_CODEC_ID_H263:
		err = h263_packetize(st, ts, &mb, st->pkth, st->arg);
		break;

	case AV_CODEC_ID_H264:
		err = h264_packetize(ts, pkt->data, pkt->size,
				     st->encprm.pktsize,
				     st->pkth, st->arg);
		break;

	case AV_CODEC_ID_MPEG4:
		err = general_packetize(ts, &mb, st->encprm.pktsize,
					st->pkth, st->arg);
		break;

#ifdef AV_CODEC_ID_H265
	case AV_CODEC_ID_H265:
		err = h265_packetize(ts, pkt->data, pkt->size,
				     st->encprm.pktsize,
				     st->pkth, st->arg);
		break;
#endif

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
