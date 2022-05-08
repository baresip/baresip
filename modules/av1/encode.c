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


#ifndef AOM_USAGE_REALTIME
#define AOM_USAGE_REALTIME (1)
#endif


enum {
	HDR_SIZE = 1,
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
	bool new;
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
	(void)fmtp;

	if (!vesp || !vc || !prm || prm->pktsize < (HDR_SIZE + 1))
		return EINVAL;

	ves = *vesp;

	if (!ves) {

		ves = mem_zalloc(sizeof(*ves), destructor);
		if (!ves)
			return ENOMEM;

		ves->picid = rand_u16();
		ves->new = true;

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

	res = aom_codec_enc_config_default(&aom_codec_av1_cx_algo, &cfg,
					   AOM_USAGE_REALTIME);
	if (res)
		return EPROTO;

	cfg.g_w               = size->w;
	cfg.g_h               = size->h;
	cfg.g_timebase.num    = 1;
	cfg.g_timebase.den    = VIDEO_TIMEBASE;
	cfg.g_threads         = 8;
	cfg.g_usage           = AOM_USAGE_REALTIME;
	cfg.g_error_resilient = AOM_ERROR_RESILIENT_DEFAULT;
	cfg.g_pass            = AOM_RC_ONE_PASS;
	cfg.g_lag_in_frames   = 0;
	cfg.rc_end_usage      = AOM_VBR;
	cfg.rc_target_bitrate = ves->bitrate / 1000;
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


static inline void hdr_encode(uint8_t hdr[HDR_SIZE],
			      bool z, bool y, uint8_t w, bool n)
{
	hdr[0] = z<<7 | y<<6 | w<<4 | n<<3;
}


static int packetize(struct videnc_state *ves, bool marker, uint64_t rtp_ts,
		     const uint8_t *buf, size_t len,
		     size_t maxlen,
		     videnc_packet_h *pkth, void *arg)
{
	uint8_t hdr[HDR_SIZE];
	bool start = true;
	uint8_t w = 0;  /* variable OBU count */
	int err = 0;

	maxlen -= sizeof(hdr);

	while (len > maxlen) {

		hdr_encode(hdr, !start, true, w, ves->new);
		ves->new = false;

		err |= pkth(false, rtp_ts, hdr, sizeof(hdr), buf, maxlen, arg);

		buf  += maxlen;
		len  -= maxlen;
		start = false;
	}

	hdr_encode(hdr, !start, false, w, ves->new);
	ves->new = false;

	err |= pkth(marker, rtp_ts, hdr, sizeof(hdr), buf, len, arg);

	return err;
}


static struct mbuf *encode_obu(uint8_t type, const uint8_t *p, size_t len)
{
	struct mbuf *mb = mbuf_alloc(1024);
	const bool has_size = false;  /* NOTE */
	int err;

	err = av1_obu_encode(mb, type, has_size, len, p);
	if (err)
		return NULL;

	mb->pos = 0;

	return mb;
}


static int copy_obus(struct mbuf *mb_pkt, const uint8_t *buf, size_t sz)
{
	struct mbuf wrap = { (uint8_t *)buf, sz, 0, sz };
	int err = 0;

	while (mbuf_get_left(&wrap) >= 2) {

		struct av1_obu_hdr hdr;
		struct mbuf *mb_obu = NULL;

		err = av1_obu_decode(&hdr, &wrap);
		if (err) {
			warning("av1: encode: hdr dec error (%m)\n", err);
			break;
		}

		switch (hdr.type) {

		case OBU_TEMPORAL_DELIMITER:
		case OBU_TILE_GROUP:
		case OBU_PADDING:
			/* skip */
			break;

		default:
#if 1
			debug("av1: encode: copy [%H]\n", av1_obu_print, &hdr);
#endif

			mb_obu = encode_obu(hdr.type, mbuf_buf(&wrap),
					    hdr.size);

			err = av1_leb128_encode(mb_pkt, mb_obu->end);
			if (err)
				return err;

			mbuf_write_mem(mb_pkt, mb_obu->buf, mb_obu->end);
			break;
		}

		mbuf_advance(&wrap, hdr.size);
		mem_deref(mb_obu);
	}

	return err;
}


int av1_encode_packet(struct videnc_state *ves, bool update,
		      const struct vidframe *frame, uint64_t timestamp)
{
	aom_enc_frame_flags_t flags = 0;
	aom_codec_iter_t iter = NULL;
	aom_codec_err_t res;
	aom_image_t *img;
	aom_img_fmt_t img_fmt;
	int err = 0, i;
	struct mbuf *mb_pkt = NULL;

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
		warning("av1: encoder: could not allocate image\n");
		err = ENOMEM;
		goto out;
	}

	for (i=0; i<3; i++) {
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
		bool marker = true;
		const aom_codec_cx_pkt_t *pkt;
		uint64_t ts;

		pkt = aom_codec_get_cx_data(&ves->ctx, &iter);
		if (!pkt)
			break;

		if (pkt->kind != AOM_CODEC_CX_FRAME_PKT)
			continue;

		ts = video_calc_rtp_timestamp_fix(pkt->data.frame.pts);

		if (!mb_pkt)
			mb_pkt = mbuf_alloc(1024);

		err = copy_obus(mb_pkt,
				pkt->data.frame.buf, pkt->data.frame.sz);
		if (err)
			goto out;

		err = packetize(ves, marker, ts,
				mb_pkt->buf,
				mb_pkt->end,
				ves->pktsize,
				ves->pkth, ves->arg);
		if (err)
			goto out;

		mb_pkt = mem_deref(mb_pkt);
	}

 out:
	mem_deref(mb_pkt);
	if (img)
		aom_img_free(img);

	return err;
}
