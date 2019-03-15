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
#include "h26x.h"
#include "avcodec.h"


#ifndef AV_INPUT_BUFFER_MIN_SIZE
#define AV_INPUT_BUFFER_MIN_SIZE FF_MIN_BUFFER_SIZE
#endif


struct picsz {
	enum h263_fmt fmt;  /**< Picture size */
	uint8_t mpi;        /**< Minimum Picture Interval (1-32) */
};


struct videnc_state {
	AVCodec *codec;
	AVCodecContext *ctx;
	AVFrame *pict;
	struct mbuf *mb;
	size_t sz_max; /* todo: figure out proper buffer size */
	struct mbuf *mb_frag;
	struct videnc_param encprm;
	struct vidsz encsize;
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


static void destructor(void *arg)
{
	struct videnc_state *st = arg;

	mem_deref(st->mb);
	mem_deref(st->mb_frag);

	if (st->ctx)
		avcodec_free_context(&st->ctx);

	if (st->pict)
		av_free(st->pict);
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


static int init_encoder(struct videnc_state *st)
{
	/*
	 * Special handling of H.264 encoder
	 */
	if (st->codec_id == AV_CODEC_ID_H264 && avcodec_h264enc) {

		st->codec = avcodec_h264enc;

		info("avcodec: h264 encoder activated\n");

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

	if (st->pict)
		av_free(st->pict);

	st->ctx = avcodec_alloc_context3(st->codec);

	st->pict = av_frame_alloc();

	if (!st->ctx || !st->pict) {
		err = ENOMEM;
		goto out;
	}

	av_opt_set_defaults(st->ctx);

	st->ctx->bit_rate  = prm->bitrate;
	st->ctx->width     = size->w;
	st->ctx->height    = size->h;
	st->ctx->pix_fmt   = pix_fmt;
	st->ctx->time_base.num = 1;
	st->ctx->time_base.den = prm->fps;

	/* params to avoid libavcodec/x264 default preset error */
	if (st->codec_id == AV_CODEC_ID_H264) {

		av_opt_set(st->ctx->priv_data, "profile", "baseline", 0);

		st->ctx->me_range = 16;
		st->ctx->qmin = 10;
		st->ctx->qmax = 51;
		st->ctx->max_qdiff = 4;

		if (st->codec == avcodec_find_encoder_by_name("nvenc_h264") ||
		st->codec == avcodec_find_encoder_by_name("h264_nvenc")) {

#if LIBAVUTIL_VERSION_INT >= ((51<<16)+(21<<8)+0)
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
#endif
		}
	}

	if (avcodec_open2(st->ctx, st->codec, NULL) < 0) {
		err = ENOENT;
		goto out;
	}

	st->pict->format = pix_fmt;
	st->pict->width = size->w;
	st->pict->height = size->h;

 out:
	if (err) {
		if (st->ctx)
			avcodec_free_context(&st->ctx);

		if (st->pict) {
			av_free(st->pict);
			st->pict = NULL;
		}
	}
	else
		st->encsize = *size;

	return err;
}


int decode_sdpparam_h264(struct videnc_state *st, const struct pl *name,
			 const struct pl *val)
{
	if (0 == pl_strcasecmp(name, "packetization-mode")) {
		st->u.h264.packetization_mode = pl_u32(val);

		if (st->u.h264.packetization_mode != 0) {
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
		err = EINVAL;
		goto out;
	}

	st->mb  = mbuf_alloc(AV_INPUT_BUFFER_MIN_SIZE * 20);
	st->mb_frag = mbuf_alloc(1024);
	if (!st->mb || !st->mb_frag) {
		err = ENOMEM;
		goto out;
	}

	st->sz_max = st->mb->size;

	err = init_encoder(st);
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
	int i, err, ret;
	int pix_fmt;
	int64_t pts;
	uint64_t ts;

	if (!st || !frame)
		return EINVAL;

	pix_fmt = vidfmt_to_avpixfmt(frame->fmt);
	if (pix_fmt == AV_PIX_FMT_NONE) {
		warning("avcodec: pixel format not supported (%s)\n",
			vidfmt_name(frame->fmt));
		return ENOTSUP;
	}

	if (!st->ctx || !vidsz_cmp(&st->encsize, &frame->size)) {

		err = open_encoder(st, &st->encprm, &frame->size, pix_fmt);
		if (err) {
			warning("avcodec: open_encoder: %m\n", err);
			return err;
		}
	}

	for (i=0; i<4; i++) {
		st->pict->data[i]     = frame->data[i];
		st->pict->linesize[i] = frame->linesize[i];
	}
	st->pict->pts = timestamp;
	if (update) {
		debug("avcodec: encoder picture update\n");
		st->pict->key_frame = 1;
		st->pict->pict_type = AV_PICTURE_TYPE_I;
	}

	mbuf_rewind(st->mb);

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)
	do {
		AVPacket *pkt;

		ret = avcodec_send_frame(st->ctx, st->pict);
		if (ret < 0)
			return EBADMSG;

		pkt = av_packet_alloc();
		if (!pkt)
			return ENOMEM;

		ret = avcodec_receive_packet(st->ctx, pkt);
		if (ret < 0) {
			av_packet_free(&pkt);
			return 0;
		}

		pts = pkt->dts;

		err = mbuf_write_mem(st->mb, pkt->data, pkt->size);
		st->mb->pos = 0;

		av_packet_free(&pkt);

		if (err)
			return err;
	} while (0);
#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(54, 1, 0)
	do {
		AVPacket avpkt;
		int got_packet;

		av_init_packet(&avpkt);

		avpkt.data = st->mb->buf;
		avpkt.size = (int)st->mb->size;

		ret = avcodec_encode_video2(st->ctx, &avpkt,
					    st->pict, &got_packet);
		if (ret < 0)
			return EBADMSG;
		if (!got_packet)
			return 0;

		mbuf_set_end(st->mb, avpkt.size);

		pts = avpkt.dts;

	} while (0);
#else
	ret = avcodec_encode_video(st->ctx, st->mb->buf,
				   (int)st->mb->size, st->pict);
	if (ret < 0 )
		return EBADMSG;

	/* todo: figure out proper buffer size */
	if (ret > (int)st->sz_max) {
		debug("avcodec: grow encode buffer %u --> %d\n",
		      st->sz_max, ret);
		st->sz_max = ret;
	}

	mbuf_set_end(st->mb, ret);

	pts = st->pict->pts;
#endif

	ts = video_calc_rtp_timestamp_fix(pts);

	switch (st->codec_id) {

	case AV_CODEC_ID_H263:
		err = h263_packetize(st, ts, st->mb, st->pkth, st->arg);
		break;

	case AV_CODEC_ID_H264:
		err = h264_packetize(ts, st->mb->buf, st->mb->end,
				     st->encprm.pktsize,
				     st->pkth, st->arg);
		break;

	case AV_CODEC_ID_MPEG4:
		err = general_packetize(ts, st->mb, st->encprm.pktsize,
					st->pkth, st->arg);
		break;

	default:
		err = EPROTO;
		break;
	}

	return err;
}
