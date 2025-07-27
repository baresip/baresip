/**
 * @file av1/encode.c AV1 Encode
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <re_av1.h>
#include <rem.h>
#include <baresip.h>
#include <aom/aom.h>
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#include "av1.h"


struct videnc_state {
	aom_codec_ctx_t ctx;
	struct vidsz size;
	double fps;
	unsigned bitrate;
	unsigned pktsize;
	bool ctxup;
	videnc_packet_h *pkth;
	const struct video *vid;
};


static void destructor(void *arg)
{
	struct videnc_state *ves = arg;

	if (ves->ctxup)
		aom_codec_destroy(&ves->ctx);
}


int av1_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, const struct video *vid)
{
	struct videnc_state *ves;
	(void)fmtp;

	if (!vesp || !vc || !prm || prm->pktsize < (AV1_AGGR_HDR_SIZE + 1))
		return EINVAL;

	ves = *vesp;

	if (!ves) {

		ves = mem_zalloc(sizeof(*ves), destructor);
		if (!ves)
			return ENOMEM;

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
	ves->vid     = vid;

	return 0;
}


static int open_encoder(struct videnc_state *ves, const struct vidsz *size)
{
	aom_codec_enc_cfg_t cfg;
	aom_codec_err_t res;

	res = aom_codec_enc_config_default(&aom_codec_av1_cx_algo, &cfg,
					   AOM_USAGE_REALTIME);
	if (res)
		return EPROTO;

	cfg.g_w               = size->w;
	cfg.g_h               = size->h;
	cfg.g_timebase.num    = 1;
	cfg.g_timebase.den    = VIDEO_TIMEBASE;
	cfg.g_threads         = 8;
	cfg.g_error_resilient = AOM_ERROR_RESILIENT_DEFAULT;
	cfg.g_pass            = AOM_RC_ONE_PASS;
	cfg.g_lag_in_frames   = 0;
	cfg.rc_end_usage      = AOM_VBR;
	cfg.rc_target_bitrate = ves->bitrate / 1000;
	cfg.kf_mode           = AOM_KF_AUTO;
#if 0
	cfg.kf_min_dist       = ves->fps * 10;
	cfg.kf_max_dist       = ves->fps * 10;
#endif

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


static int packetize_rtp(struct videnc_state *ves,
			 bool keyframe, uint64_t rtp_ts,
			 const uint8_t *buf, size_t size)
{
	bool new_flag = keyframe;

	return av1_packetize(&new_flag, true, rtp_ts, buf, size,
			     ves->pktsize, (av1_packet_h *)ves->pkth,
			     (void *)ves->vid);
}


int av1_encode_packet(struct videnc_state *ves, bool update,
		      const struct vidframe *frame, uint64_t timestamp)
{
	aom_enc_frame_flags_t flags = 0;
	aom_codec_iter_t iter = NULL;
	aom_codec_err_t res;
	aom_image_t *img;
	aom_img_fmt_t img_fmt;
	int err = 0;

	if (!ves || !frame || frame->fmt != VID_FMT_YUV420P)
		return EINVAL;

	if (!ves->ctxup || !vidsz_cmp(&ves->size, &frame->size)) {

		err = open_encoder(ves, &frame->size);
		if (err)
			return err;

		ves->size = frame->size;
	}

	if (update) {
		debug("av1: picture update\n");
		flags |= AOM_EFLAG_FORCE_KF;
	}

	img_fmt = AOM_IMG_FMT_I420;

	img = aom_img_wrap(NULL, img_fmt, frame->size.w, frame->size.h,
			   16, NULL);
	if (!img) {
		warning("av1: encoder: could not allocate image\n");
		err = ENOMEM;
		goto out;
	}

	for (unsigned i=0; i<3; i++) {
		img->stride[i] = frame->linesize[i];
		img->planes[i] = frame->data[i];
	}

	res = aom_codec_encode(&ves->ctx, img, timestamp, 1, flags);
	if (res) {
		warning("av1: enc error: %s\n", aom_codec_err_to_string(res));
		err = ENOMEM;
		goto out;
	}

	for (;;) {
		const aom_codec_cx_pkt_t *pkt;
		uint64_t rtp_ts;
		bool keyframe = false;

		pkt = aom_codec_get_cx_data(&ves->ctx, &iter);
		if (!pkt)
			break;

		if (pkt->kind != AOM_CODEC_CX_FRAME_PKT)
			continue;

		if (pkt->data.frame.flags & AOM_FRAME_IS_KEY) {
			keyframe = true;
			debug("av1: encode: keyframe\n");
		}

		rtp_ts = video_calc_rtp_timestamp_fix(pkt->data.frame.pts);

		err = packetize_rtp(ves, keyframe, rtp_ts,
				    pkt->data.frame.buf, pkt->data.frame.sz);
		if (err)
			break;
	}

 out:
	if (img)
		aom_img_free(img);

	return err;
}


int av1_encode_packetize(struct videnc_state *ves,
			 const struct vidpacket *packet)
{
	uint64_t rtp_ts;
	int err;

	if (!ves || !packet)
		return EINVAL;

	rtp_ts = video_calc_rtp_timestamp_fix(packet->timestamp);

	err = packetize_rtp(ves, packet->keyframe, rtp_ts,
			    packet->buf, packet->size);

	return err;
}
