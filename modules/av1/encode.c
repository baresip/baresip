/**
 * @file av1/encode.c AV1 Encode
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <aom/aom.h>
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#include "av1.h"


enum {
	HDR_SIZE = 4,
};


struct videnc_state {
	aom_codec_ctx_t ctx;
	struct vidsz size;
	double fps;
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
		aom_codec_destroy(&ves->ctx);
}


int av1_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, void *arg)
{
	struct videnc_state *ves;

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

			aom_codec_destroy(&ves->ctx);
			ves->ctxup = false;
		}
	}

	ves->bitrate = prm->bitrate;
	ves->pktsize = prm->pktsize;
	ves->fps     = prm->fps;
	ves->pkth    = pkth;
	ves->arg     = arg;

	return 0;
}


static int open_encoder(struct videnc_state *ves, const struct vidsz *size)
{
	aom_codec_enc_cfg_t cfg;
	aom_codec_err_t res;

	res = aom_codec_enc_config_default(&aom_codec_av1_cx_algo, &cfg, 0);
	if (res)
		return EPROTO;

	cfg.g_w               = size->w;
	cfg.g_h               = size->h;
	cfg.g_timebase.num    = 1;
	cfg.g_timebase.den    = ves->fps;
	cfg.g_error_resilient = AOM_ERROR_RESILIENT_DEFAULT;
	cfg.g_pass            = AOM_RC_ONE_PASS;
	cfg.g_lag_in_frames   = 0;
	cfg.rc_end_usage      = AOM_VBR;
	cfg.rc_target_bitrate = ves->bitrate;
	cfg.kf_mode           = AOM_KF_AUTO;

	if (ves->ctxup) {
		debug("av1: re-opening encoder\n");
		aom_codec_destroy(&ves->ctx);
		ves->ctxup = false;
	}

	res = aom_codec_enc_init(&ves->ctx, &aom_codec_av1_cx_algo, &cfg,
				 0);
	if (res) {
		warning("av1: enc init: %s\n", aom_codec_err_to_string(res));
		return EPROTO;
	}

	ves->ctxup = true;

	res = aom_codec_control(&ves->ctx, AOME_SET_CPUUSED, 8);
	if (res) {
		warning("av1: codec ctrl C: %s\n",
			aom_codec_err_to_string(res));
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


static inline int packetize(bool marker, uint64_t rtp_ts,
			    const uint8_t *buf, size_t len,
			    size_t maxlen, bool noref, uint8_t partid,
			    uint16_t picid, videnc_packet_h *pkth, void *arg)
{
	uint8_t hdr[HDR_SIZE];
	bool start = true;
	int err = 0;

	maxlen -= sizeof(hdr);

	while (len > maxlen) {

		hdr_encode(hdr, noref, start, partid, picid);

		err |= pkth(false, rtp_ts, hdr, sizeof(hdr), buf, maxlen, arg);

		buf  += maxlen;
		len  -= maxlen;
		start = false;
	}

	hdr_encode(hdr, noref, start, partid, picid);

	err |= pkth(marker, rtp_ts, hdr, sizeof(hdr), buf, len, arg);

	return err;
}


int av1_encode(struct videnc_state *ves, bool update,
		const struct vidframe *frame, uint64_t timestamp)
{
	aom_enc_frame_flags_t flags = 0;
	aom_codec_iter_t iter = NULL;
	aom_codec_err_t res;
	aom_image_t *img;
	aom_img_fmt_t img_fmt;
	int err = 0, i;

	if (!ves || !frame || frame->fmt != VID_FMT_YUV420P)
		return EINVAL;

	if (!ves->ctxup || !vidsz_cmp(&ves->size, &frame->size)) {

		err = open_encoder(ves, &frame->size);
		if (err)
			return err;

		ves->size = frame->size;
	}

	if (update) {
		/* debug("av1: picture update\n"); */
		flags |= AOM_EFLAG_FORCE_KF;
	}

	img_fmt = AOM_IMG_FMT_I420;

	img = aom_img_wrap(NULL, img_fmt, frame->size.w, frame->size.h,
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

	res = aom_codec_encode(&ves->ctx, img, timestamp, 1,
			       flags);
	if (res) {
		warning("av1: enc error: %s\n", aom_codec_err_to_string(res));
		return ENOMEM;
	}

	++ves->picid;

	for (;;) {
		bool keyframe = false, marker = true;
		const aom_codec_cx_pkt_t *pkt;
		uint8_t partid = 0;
		uint64_t ts;

		pkt = aom_codec_get_cx_data(&ves->ctx, &iter);
		if (!pkt)
			break;

		if (pkt->kind != AOM_CODEC_CX_FRAME_PKT)
			continue;

		if (pkt->data.frame.flags & AOM_FRAME_IS_KEY)
			keyframe = true;

		if (pkt->data.frame.flags & AOM_FRAME_IS_FRAGMENT)
			marker = false;

		if (pkt->data.frame.partition_id >= 0)
			partid = pkt->data.frame.partition_id;

		ts = video_calc_rtp_timestamp_fix(pkt->data.frame.pts);

		err = packetize(marker, ts,
				pkt->data.frame.buf,
				pkt->data.frame.sz,
				ves->pktsize, !keyframe, partid, ves->picid,
				ves->pkth, ves->arg);
		if (err)
			return err;
	}

 out:
	if (img)
		aom_img_free(img);

	return err;
}
