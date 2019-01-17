/**
 * @file vp8/encode.c VP8 Encode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#include "vp8.h"


enum {
	HDR_SIZE = 4,
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
};


static void destructor(void *arg)
{
	struct videnc_state *ves = arg;

	if (ves->ctxup)
		vpx_codec_destroy(&ves->ctx);
}


int vp8_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, void *arg)
{
	const struct vp8_vidcodec *vp8 = (struct vp8_vidcodec *)vc;
	struct videnc_state *ves;
	uint32_t max_fs;
	(void)vp8;

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

	max_fs = vp8_max_fs(fmtp);
	if (max_fs > 0)
		prm->max_fs = max_fs * 256;

	return 0;
}


static int open_encoder(struct videnc_state *ves, const struct vidsz *size)
{
	vpx_codec_enc_cfg_t cfg;
	vpx_codec_err_t res;
	vpx_codec_flags_t flags = 0;

	res = vpx_codec_enc_config_default(&vpx_codec_vp8_cx_algo, &cfg, 0);
	if (res)
		return EPROTO;

	cfg.g_profile = 2;
	cfg.g_w = size->w;
	cfg.g_h = size->h;
	cfg.g_timebase.num    = 1;
	cfg.g_timebase.den    = ves->fps;
#ifdef VPX_ERROR_RESILIENT_DEFAULT
	cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
#endif
	cfg.g_pass            = VPX_RC_ONE_PASS;
	cfg.g_lag_in_frames   = 0;
	cfg.rc_end_usage      = VPX_VBR;
	cfg.rc_target_bitrate = ves->bitrate;
	cfg.kf_mode           = VPX_KF_AUTO;

	if (ves->ctxup) {
		debug("vp8: re-opening encoder\n");
		vpx_codec_destroy(&ves->ctx);
		ves->ctxup = false;
	}

#ifdef VPX_CODEC_USE_OUTPUT_PARTITION
	flags |= VPX_CODEC_USE_OUTPUT_PARTITION;
#endif

	res = vpx_codec_enc_init(&ves->ctx, &vpx_codec_vp8_cx_algo, &cfg,
				 flags);
	if (res) {
		warning("vp8: enc init: %s\n", vpx_codec_err_to_string(res));
		return EPROTO;
	}

	ves->ctxup = true;

	res = vpx_codec_control(&ves->ctx, VP8E_SET_CPUUSED, 16);
	if (res) {
		warning("vp8: codec ctrl: %s\n", vpx_codec_err_to_string(res));
	}

	res = vpx_codec_control(&ves->ctx, VP8E_SET_NOISE_SENSITIVITY, 0);
	if (res) {
		warning("vp8: codec ctrl: %s\n", vpx_codec_err_to_string(res));
	}

	return 0;
}


static inline void hdr_encode(uint8_t hdr[HDR_SIZE], bool noref, bool start,
			      uint8_t partid, uint16_t picid)
{
	hdr[0] = 1<<7 | noref<<5 | start<<4 | (partid & 0x7);
	hdr[1] = 1<<7;
	hdr[2] = 1<<7 | (picid>>8 & 0x7f);
	hdr[3] = picid & 0xff;
}


static inline int packetize(bool marker, const uint8_t *buf, size_t len,
			    size_t maxlen, bool noref, uint8_t partid,
			    uint16_t picid, uint64_t rtp_ts,
			    videnc_packet_h *pkth, void *arg)
{
	uint8_t hdr[HDR_SIZE];
	bool start = true;
	int err = 0;

	maxlen -= sizeof(hdr);

	while (len > maxlen) {

		hdr_encode(hdr, noref, start, partid, picid);

		err |= pkth(false, rtp_ts, hdr, sizeof(hdr), buf, maxlen,
			    arg);

		buf  += maxlen;
		len  -= maxlen;
		start = false;
	}

	hdr_encode(hdr, noref, start, partid, picid);

	err |= pkth(marker, rtp_ts, hdr, sizeof(hdr), buf, len, arg);

	return err;
}


int vp8_encode(struct videnc_state *ves, bool update,
	       const struct vidframe *frame, uint64_t timestamp)
{
	vpx_enc_frame_flags_t flags = 0;
	vpx_codec_iter_t iter = NULL;
	vpx_codec_err_t res;
	vpx_image_t img;
	int err, i;

	if (!ves || !frame || frame->fmt != VID_FMT_YUV420P)
		return EINVAL;

	if (!ves->ctxup || !vidsz_cmp(&ves->size, &frame->size)) {

		err = open_encoder(ves, &frame->size);
		if (err)
			return err;

		ves->size = frame->size;
	}

	if (update) {
		/* debug("vp8: picture update\n"); */
		flags |= VPX_EFLAG_FORCE_KF;
	}

	memset(&img, 0, sizeof(img));

	img.fmt = VPX_IMG_FMT_I420;
	img.w = img.d_w = frame->size.w;
	img.h = img.d_h = frame->size.h;

	for (i=0; i<4; i++) {
		img.stride[i] = frame->linesize[i];
		img.planes[i] = frame->data[i];
	}

	res = vpx_codec_encode(&ves->ctx, &img, timestamp, 1,
			       flags, VPX_DL_REALTIME);
	if (res) {
		warning("vp8: enc error: %s\n", vpx_codec_err_to_string(res));
		return ENOMEM;
	}

	++ves->picid;

	for (;;) {
		bool keyframe = false, marker = true;
		const vpx_codec_cx_pkt_t *pkt;
		uint8_t partid = 0;
		uint64_t ts;

		pkt = vpx_codec_get_cx_data(&ves->ctx, &iter);
		if (!pkt)
			break;

		if (pkt->kind != VPX_CODEC_CX_FRAME_PKT)
			continue;

		if (pkt->data.frame.flags & VPX_FRAME_IS_KEY)
			keyframe = true;

#ifdef VPX_FRAME_IS_FRAGMENT
		if (pkt->data.frame.flags & VPX_FRAME_IS_FRAGMENT)
			marker = false;

		if (pkt->data.frame.partition_id >= 0)
			partid = pkt->data.frame.partition_id;
#endif

		/*
		 * convert PTS to RTP Timestamp
		 */
		ts =  video_calc_rtp_timestamp_fix(pkt->data.frame.pts);

		err = packetize(marker,
				pkt->data.frame.buf,
				pkt->data.frame.sz,
				ves->pktsize, !keyframe, partid, ves->picid,
				ts,
				ves->pkth, ves->arg);
		if (err)
			return err;
	}

	return 0;
}
