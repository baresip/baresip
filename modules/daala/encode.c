/**
 * @file daala/encode.c  Experimental video-codec using Daala -- encoder
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <daala/daalaenc.h>
#include "daala.h"


struct videnc_state {
	struct vidsz size;
	daala_enc_ctx *enc;
	int64_t pts;
	unsigned fps;
	unsigned bitrate;
	unsigned pktsize;
	videnc_packet_h *pkth;
	void *arg;

	struct {
		bool valid;
		size_t n_frame;
		size_t n_header;
		size_t n_keyframe;
		size_t n_packet;
	} stats;
};


static void dump_stats(const struct videnc_state *ves)
{
	re_printf("~~~~~ Daala Encoder stats ~~~~~\n");
	re_printf("num frames:          %zu\n", ves->stats.n_frame);
	re_printf("num headers:         %zu\n", ves->stats.n_header);
	re_printf("key-frames packets:  %zu\n", ves->stats.n_keyframe);
	re_printf("total packets:       %zu\n", ves->stats.n_packet);
}


static int send_packet(struct videnc_state *ves, bool marker,
		       uint64_t timestamp, const uint8_t *pld, size_t pld_len)
{
	daala_packet dp;
	uint64_t rtp_ts;
	int err;

	memset(&dp, 0, sizeof(dp));

	dp.packet = (uint8_t *)pld;
	dp.bytes = pld_len;
	dp.b_o_s = marker;

	rtp_ts = video_calc_rtp_timestamp_fix(timestamp);

	err = ves->pkth(marker, rtp_ts, NULL, 0, pld, pld_len, ves->arg);
	if (err)
		return err;

	++ves->stats.n_packet;
	++ves->stats.valid;

	if (daala_packet_isheader(&dp))
		++ves->stats.n_header;
	else if (daala_packet_iskeyframe(&dp) > 0)
		++ves->stats.n_keyframe;

	return 0;
}


static void destructor(void *arg)
{
	struct videnc_state *ves = arg;

	if (ves->stats.valid)
		dump_stats(ves);

	if (ves->enc)
		daala_encode_free(ves->enc);
}


int daala_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
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

		*vesp = ves;
	}
	else {
		if (ves->enc && (ves->bitrate != prm->bitrate ||
				 ves->pktsize != prm->pktsize ||
				 ves->fps     != prm->fps)) {

			info("daala: encoder: params changed\n");

			daala_encode_free(ves->enc);
			ves->enc = NULL;
		}
	}

	ves->bitrate = prm->bitrate;
	ves->pktsize = prm->pktsize;
	ves->fps     = prm->fps;
	ves->pkth    = pkth;
	ves->arg     = arg;

	return 0;
}


static int open_encoder(struct videnc_state *ves, const struct vidsz *size,
			uint64_t timestamp)
{
	daala_info di;
	daala_comment dc;
	daala_packet dp;
	int err = 0;
	int complexity = 7;
	int video_q = 30;
	int bitrate = ves->bitrate;

	info("daala: open encoder (%d x %d, %d bps)\n",
	     size->w, size->h, bitrate);

	if (ves->enc) {
		debug("daala: re-opening encoder\n");
		daala_encode_free(ves->enc);
	}

	daala_info_init(&di);
	daala_comment_init(&dc);

	di.pic_width = size->w;
	di.pic_height = size->h;
	di.timebase_numerator = 1;
	di.timebase_denominator = ves->fps;
	di.frame_duration = 1;
	di.pixel_aspect_numerator = -1;
	di.pixel_aspect_denominator = -1;
	di.nplanes = 3;
	di.plane_info[0].xdec = 0;  /* YUV420P */
	di.plane_info[0].ydec = 0;
	di.plane_info[1].xdec = 1;
	di.plane_info[1].ydec = 1;
	di.plane_info[2].xdec = 1;
	di.plane_info[2].ydec = 1;

	di.keyframe_rate = 100;

	info("daala: open encoder with bitstream version %u.%u.%u\n",
	     di.version_major, di.version_minor, di.version_sub);

	ves->enc = daala_encode_create(&di);
	if (!ves->enc) {
		warning("daala: failed to open DAALA encoder\n");
		return ENOMEM;
	}

	daala_encode_ctl(ves->enc, OD_SET_QUANT,
			 &video_q, sizeof(video_q));

	daala_encode_ctl(ves->enc, OD_SET_COMPLEXITY,
			 &complexity, sizeof(complexity));

	daala_encode_ctl(ves->enc, OD_SET_BITRATE,
			 &bitrate, sizeof(bitrate));

	for (;;) {
		int r;

		r = daala_encode_flush_header(ves->enc, &dc, &dp);
		if (r < 0) {
			warning("daala: flush_header returned %d\n", r);
			break;
		}
		else if (r == 0)
			break;

		debug("daala: header: %lld bytes header=%d key=%d\n",
			  dp.bytes,
			  daala_packet_isheader(&dp),
			  daala_packet_iskeyframe(&dp));

#if 0
		re_printf("bos=%lld, eos=%lld, granule=%lld, packetno=%lld\n",
			  dp.b_o_s,
			  dp.e_o_s,
			  dp.granulepos,
			  dp.packetno);
#endif

		err = send_packet(ves, dp.b_o_s, timestamp,
				  dp.packet, dp.bytes);
		if (err)
			break;
	}

	daala_info_clear(&di);
	daala_comment_clear(&dc);

	return err;
}


int daala_encode(struct videnc_state *ves, bool update,
		 const struct vidframe *frame, uint64_t timestamp)
{
	int r, err = 0;
	daala_image img;
	unsigned i;
	(void)update;  /* XXX: how to force a KEY-frame? */

	if (!ves || !frame || frame->fmt != VID_FMT_YUV420P)
		return EINVAL;

	++ves->stats.n_frame;

	if (!ves->enc || !vidsz_cmp(&ves->size, &frame->size)) {

		err = open_encoder(ves, &frame->size, timestamp);
		if (err)
			return err;

		ves->size = frame->size;
	}

	img.planes[0].data = frame->data[0];
	img.planes[0].xdec = 0;
	img.planes[0].ydec = 0;
	img.planes[0].xstride = 1;
	img.planes[0].ystride = frame->linesize[0];

	img.planes[1].data = frame->data[1];
	img.planes[1].xdec = 1;
	img.planes[1].ydec = 1;
	img.planes[1].xstride = 1;
	img.planes[1].ystride = frame->linesize[1];

	img.planes[2].data = frame->data[2];
	img.planes[2].xdec = 1;
	img.planes[2].ydec = 1;
	img.planes[2].xstride = 1;
	img.planes[2].ystride = frame->linesize[2];

	for (i=0; i<3; i++)
		img.planes[i].bitdepth = 8;

	img.nplanes = 3;

	img.width = frame->size.w;
	img.height = frame->size.h;

	r = daala_encode_img_in(ves->enc, &img, 0);
	if (r != 0) {
		warning("daala: encoder: encode_img_in failed (ret = %d)\n",
			r);
		return EPROTO;
	}

	for (;;) {
		daala_packet dp;

		r = daala_encode_packet_out(ves->enc, 0, &dp);
		if (r < 0) {
			warning("daala: encoder: packet_out ret=%d\n", r);
			break;
		}
		else if (r == 0) {
			break;
		}

		err = send_packet(ves, dp.b_o_s, timestamp,
				  dp.packet, dp.bytes);
		if (err)
			break;
	}

	return 0;
}
