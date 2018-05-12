/**
 * @file vp9/encode.c VP9 Encode
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#include "vp9.h"


enum {
	HDR_SIZE = 3,
};


struct videnc_state {
	vpx_codec_ctx_t ctx;
	struct vidsz size;
	unsigned fps;
	unsigned bitrate;
	unsigned pktsize;
	bool ctxup;
	uint16_t picid;
	videnc_packet_h *pkth;
	void *arg;

	unsigned n_frames;
	unsigned n_key_frames;
	size_t n_bytes;
};


static void destructor(void *arg)
{
	struct videnc_state *ves = arg;

	if (ves->ctxup) {

		debug("vp9: encoder stats:"
		      " frames=%u, key_frames=%u, bytes=%zu\n",
		      ves->n_frames,
		      ves->n_key_frames,
		      ves->n_bytes);

		vpx_codec_destroy(&ves->ctx);
	}
}


int vp9_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, void *arg)
{
	const struct vp9_vidcodec *vp9 = (struct vp9_vidcodec *)vc;
	struct videnc_state *ves;
	uint32_t max_fs;
	(void)vp9;

	if (!vesp || !vc || !prm || prm->pktsize < (HDR_SIZE + 1))
		return EINVAL;

	ves = *vesp;

	if (!ves) {

		ves = mem_zalloc(sizeof(*ves), destructor);
		if (!ves)
			return ENOMEM;

		ves->picid = rand_u16();

		*vesp = ves;
	}
	else {
		if (ves->ctxup && (ves->bitrate != prm->bitrate ||
				   ves->fps     != prm->fps)) {

			vpx_codec_destroy(&ves->ctx);
			ves->ctxup = false;
		}
	}

	ves->bitrate = prm->bitrate;
	ves->pktsize = prm->pktsize;
	ves->fps     = prm->fps;
	ves->pkth    = pkth;
	ves->arg     = arg;

	max_fs = vp9_max_fs(fmtp);
	if (max_fs > 0)
		prm->max_fs = max_fs * 256;

	return 0;
}


static int open_encoder(struct videnc_state *ves, const struct vidsz *size)
{
	vpx_codec_enc_cfg_t cfg;
	vpx_codec_err_t res;

	res = vpx_codec_enc_config_default(&vpx_codec_vp9_cx_algo, &cfg, 0);
	if (res)
		return EPROTO;

	/*
	  Profile 0 = 8 bit yuv420p
	  Profile 1 = 8 bit yuv422/440/444p
	  Profile 2 = 10/12 bit yuv420p
	  Profile 3 = 10/12 bit yuv422/440/444p
	 */

	cfg.g_profile         = 0;
	cfg.g_w               = size->w;
	cfg.g_h               = size->h;
	cfg.g_timebase.num    = 1;
	cfg.g_timebase.den    = ves->fps;
	cfg.rc_target_bitrate = ves->bitrate / 1000;
	cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
	cfg.g_pass            = VPX_RC_ONE_PASS;
	cfg.g_lag_in_frames   = 0;
	cfg.rc_end_usage      = VPX_VBR;
	cfg.kf_mode           = VPX_KF_AUTO;

	if (ves->ctxup) {
		debug("vp9: re-opening encoder\n");
		vpx_codec_destroy(&ves->ctx);
		ves->ctxup = false;
	}

	res = vpx_codec_enc_init(&ves->ctx, &vpx_codec_vp9_cx_algo, &cfg,
				 0);
	if (res) {
		warning("vp9: enc init: %s\n", vpx_codec_err_to_string(res));
		return EPROTO;
	}

	ves->ctxup = true;

	res = vpx_codec_control(&ves->ctx, VP8E_SET_CPUUSED, 8);
	if (res) {
		warning("vp9: codec ctrl: %s\n", vpx_codec_err_to_string(res));
	}
#ifdef VP9E_SET_NOISE_SENSITIVITY
	res = vpx_codec_control(&ves->ctx, VP9E_SET_NOISE_SENSITIVITY, 0);
	if (res) {
		warning("vp9: codec ctrl: %s\n", vpx_codec_err_to_string(res));
	}
#endif

	info("vp9: encoder opened, picture size %u x %u\n", size->w, size->h);

	return 0;
}


static inline void hdr_encode(uint8_t hdr[HDR_SIZE], bool start, bool end,
			      uint16_t picid)
{
	hdr[0] = 1<<7 | start<<3 | end<<2;
	hdr[1] = 1<<7 | (picid>>8 & 0x7f);
	hdr[2] = picid & 0xff;
}


static int send_packet(struct videnc_state *ves, bool marker,
		       const uint8_t *hdr, size_t hdr_len,
		       const uint8_t *pld, size_t pld_len,
		       uint64_t rtp_ts)
{
	ves->n_bytes += (hdr_len + pld_len);

	return ves->pkth(marker, rtp_ts, hdr, hdr_len, pld, pld_len,
			 ves->arg);
}


static inline int packetize(struct videnc_state *ves,
			    bool marker, const uint8_t *buf, size_t len,
			    size_t maxlen, uint16_t picid,
			    uint64_t rtp_ts)
{
	uint8_t hdr[HDR_SIZE];
	bool start = true;
	int err = 0;

	maxlen -= sizeof(hdr);

	while (len > maxlen) {

		hdr_encode(hdr, start, false, picid);

		err |= send_packet(ves, false, hdr, sizeof(hdr), buf, maxlen,
				   rtp_ts);

		buf  += maxlen;
		len  -= maxlen;
		start = false;
	}

	hdr_encode(hdr, start, true, picid);

	err |= send_packet(ves, marker, hdr, sizeof(hdr), buf, len,
			   rtp_ts);

	return err;
}


int vp9_encode(struct videnc_state *ves, bool update,
	       const struct vidframe *frame, uint64_t timestamp)
{
	vpx_enc_frame_flags_t flags = 0;
	vpx_codec_iter_t iter = NULL;
	vpx_codec_err_t res;
	vpx_image_t *img = NULL;
	vpx_img_fmt_t img_fmt;
	int err, i;

	if (!ves || !frame)
		return EINVAL;

	switch (frame->fmt) {

	case VID_FMT_YUV420P:
		img_fmt = VPX_IMG_FMT_I420;
		break;

	default:
		warning("vp9: pixel format not supported (%s)\n",
			vidfmt_name(frame->fmt));
		return EINVAL;
	}

	if (!ves->ctxup || !vidsz_cmp(&ves->size, &frame->size)) {

		err = open_encoder(ves, &frame->size);
		if (err)
			return err;

		ves->size = frame->size;
	}

	++ves->n_frames;

	if (update) {
		/* debug("vp9: picture update\n"); */
		flags |= VPX_EFLAG_FORCE_KF;
	}

	img = vpx_img_wrap(NULL, img_fmt, frame->size.w, frame->size.h,
			   16, NULL);
	if (!img) {
		warning("vp9: encoder: could not allocate image\n");
		err = ENOMEM;
		goto out;
	}

	for (i=0; i<4; i++) {
		img->stride[i] = frame->linesize[i];
		img->planes[i] = frame->data[i];
	}

	res = vpx_codec_encode(&ves->ctx, img, timestamp, 1,
			       flags, VPX_DL_REALTIME);
	if (res) {
		warning("vp9: enc error: %s\n", vpx_codec_err_to_string(res));
		err = ENOMEM;
		goto out;
	}

	++ves->picid;

	for (;;) {
		bool marker = true;
		const vpx_codec_cx_pkt_t *pkt;
		uint64_t ts;

		pkt = vpx_codec_get_cx_data(&ves->ctx, &iter);
		if (!pkt)
			break;

		if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) {
			continue;
		}

		if (pkt->data.frame.flags & VPX_FRAME_IS_KEY) {
			++ves->n_key_frames;
		}

		if (pkt->data.frame.flags & VPX_FRAME_IS_FRAGMENT)
			marker = false;

		ts = video_calc_rtp_timestamp_fix(pkt->data.frame.pts);

		err = packetize(ves,
				marker,
				pkt->data.frame.buf,
				pkt->data.frame.sz,
				ves->pktsize, ves->picid,
				ts);
		if (err)
			return err;
	}

 out:
	if (img)
		vpx_img_free(img);

	return err;
}
