/**
 * @file h265/encode.c H.265 Encode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include "h265.h"


struct videnc_state {
	struct vidsz size;
	enum vidfmt fmt;
	AVCodecContext *ctx;
	double fps;
	unsigned bitrate;
	unsigned pktsize;
	videnc_packet_h *pkth;
	void *arg;
};


static void destructor(void *arg)
{
	struct videnc_state *st = arg;

	if (st->ctx)
		avcodec_free_context(&st->ctx);
}


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


int h265_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		       struct videnc_param *prm, const char *fmtp,
		       videnc_packet_h *pkth, void *arg)
{
	struct videnc_state *ves;
	(void)fmtp;

	if (!vesp || !vc || !prm || prm->pktsize < 3 || !pkth)
		return EINVAL;

	ves = *vesp;

	if (!ves) {

		ves = mem_zalloc(sizeof(*ves), destructor);
		if (!ves)
			return ENOMEM;

		ves->fmt = -1;

		*vesp = ves;
	}
	else {
		if (ves->ctx && (ves->bitrate != prm->bitrate ||
				 ves->pktsize != prm->pktsize ||
				 ves->fps     != prm->fps)) {

			avcodec_free_context(&ves->ctx);
		}
	}

	ves->bitrate = prm->bitrate;
	ves->pktsize = prm->pktsize;
	ves->fps     = prm->fps;
	ves->pkth    = pkth;
	ves->arg     = arg;

	return 0;
}


static int open_encoder(struct videnc_state *st, const struct vidsz *size,
			enum AVPixelFormat pix_fmt)
{
	int ret, err = 0;

	if (st->ctx)
		avcodec_free_context(&st->ctx);

	st->ctx = avcodec_alloc_context3(h265_encoder);
	if (!st->ctx) {
		err = ENOMEM;
		goto out;
	}

	av_opt_set_defaults(st->ctx);

	st->ctx->bit_rate  = st->bitrate;
	st->ctx->width     = size->w;
	st->ctx->height    = size->h;
	st->ctx->pix_fmt   = pix_fmt;

	st->ctx->time_base.num = 1;
	st->ctx->time_base.den = st->fps;
	st->ctx->gop_size = 10 * st->fps;

	if (0 == strcmp(h265_encoder->name, "libx265")) {

		av_opt_set(st->ctx->priv_data, "profile", "main444-8", 0);
		av_opt_set(st->ctx->priv_data, "preset", "ultrafast", 0);
		av_opt_set(st->ctx->priv_data, "tune", "zerolatency", 0);
	}

	ret = avcodec_open2(st->ctx, h265_encoder, NULL);
	if (ret < 0) {
		warning("h265: encoder: avcodec open failed ret=%d\n", ret);
		err = ENOENT;
		goto out;
	}

 out:
	if (err) {
		if (st->ctx)
			avcodec_free_context(&st->ctx);
	}

	return err;
}


static inline int packetize(bool marker, const uint8_t *buf, size_t len,
			    size_t maxlen, uint64_t rtp_ts,
			    videnc_packet_h *pkth, void *arg)
{
	int err = 0;

	if (len <= maxlen) {
		err = pkth(marker, rtp_ts, NULL, 0, buf, len, arg);
	}
	else {
		struct h265_nal nal;
		uint8_t fu_hdr[3];
		const size_t flen = maxlen - sizeof(fu_hdr);

		err = h265_nal_decode(&nal, buf);
		if (err) {
			warning("h265: encode: could not decode"
				" NAL of %zu bytes (%m)\n", len, err);
			return err;
		}

		h265_nal_encode(fu_hdr, H265_NAL_FU,
				nal.nuh_temporal_id_plus1);

		fu_hdr[2] = 1<<7 | nal.nal_unit_type;

		buf+=2;
		len-=2;

		while (len > flen) {
			err |= pkth(false, rtp_ts, fu_hdr, 3, buf, flen,
				    arg);

			buf += flen;
			len -= flen;
			fu_hdr[2] &= ~(1 << 7); /* clear Start bit */
		}

		fu_hdr[2] |= 1<<6;  /* set END bit */

		err |= pkth(marker, rtp_ts, fu_hdr, 3, buf, len,
			    arg);
	}

	return err;
}


static int packetize_annexb(uint64_t rtp_ts, const uint8_t *buf, size_t len,
			    size_t pktsize, videnc_packet_h *pkth, void *arg)
{
	const uint8_t *start = buf;
	const uint8_t *end   = buf + len;
	const uint8_t *r;
	int err = 0;

	r = h265_find_startcode(start, end);

	while (r < end) {
		const uint8_t *r1;
		bool marker;

		/* skip zeros */
		while (!*(r++))
			;

		r1 = h265_find_startcode(r, end);

		marker = (r1 >= end);

		err |= packetize(marker, r, r1-r, pktsize, rtp_ts, pkth, arg);

		r = r1;
	}

	return err;
}


int h265_encode(struct videnc_state *st, bool update,
		const struct vidframe *frame, uint64_t timestamp)
{
	AVFrame *pict = NULL;
	AVPacket *pkt = NULL;
	uint64_t rtp_ts;
	int i, ret, got_packet = 0, err = 0;

	if (!st || !frame)
		return EINVAL;

	if (!st->ctx || !vidsz_cmp(&st->size, &frame->size) ||
	    st->fmt != frame->fmt) {

		enum AVPixelFormat pix_fmt;

		pix_fmt = vidfmt_to_avpixfmt(frame->fmt);
		if (pix_fmt == AV_PIX_FMT_NONE) {
			warning("h265: encode: pixel format not supported"
				" (%s)\n", vidfmt_name(frame->fmt));
			return ENOTSUP;
		}

		debug("h265: encoder: reset %u x %u (%s)\n",
		      frame->size.w, frame->size.h, vidfmt_name(frame->fmt));

		err = open_encoder(st, &frame->size, pix_fmt);
		if (err)
			return err;

		st->size = frame->size;
		st->fmt = frame->fmt;
	}

	pict = av_frame_alloc();
	if (!pict) {
		err = ENOMEM;
		goto out;
	}

	pict->format = st->ctx->pix_fmt;
	pict->width = frame->size.w;
	pict->height = frame->size.h;
	pict->pts = timestamp;

	for (i=0; i<4; i++) {
		pict->data[i]     = frame->data[i];
		pict->linesize[i] = frame->linesize[i];
	}

	if (update) {
		debug("h265: encoder picture update\n");
		pict->key_frame = 1;
		pict->pict_type = AV_PICTURE_TYPE_I;
	}

#if LIBAVUTIL_VERSION_MAJOR >= 55
	pict->color_range = AVCOL_RANGE_MPEG;
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

	pkt = av_packet_alloc();
	if (!pkt) {
		err = ENOMEM;
		goto out;
	}

	ret = avcodec_send_frame(st->ctx, pict);
	if (ret < 0) {
		err = EBADMSG;
		goto out;
	}

	/* NOTE: packet contains 4-byte startcode */
	ret = avcodec_receive_packet(st->ctx, pkt);
	if (ret < 0) {
		info("h265: no packet yet ..\n");
		err = 0;
		goto out;
	}

	got_packet = 1;
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
#endif

	if (!got_packet)
		goto out;

	rtp_ts = video_calc_rtp_timestamp_fix(pkt->dts);

	err = packetize_annexb(rtp_ts, pkt->data, pkt->size,
			       st->pktsize, st->pkth, st->arg);
	if (err)
		goto out;

 out:
	if (pict)
		av_free(pict);
	if (pkt)
		av_packet_free(&pkt);

	return err;
}
