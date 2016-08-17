/**
 * @file daala/decode.c  Experimental video-codec using Daala -- decoder
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <daala/daaladec.h>
#include "daala.h"


struct viddec_state {
	daala_dec_ctx *dec;

	bool got_headers;

	daala_info di;
	daala_comment dc;
	daala_setup_info *ds;

	struct {
		bool valid;
		size_t n_frame;
		size_t n_header;
		size_t n_keyframe;
		size_t n_packet;
	} stats;
};


static void dump_stats(const struct viddec_state *vds)
{
	re_printf("~~~~~ Daala Decoder stats ~~~~~\n");
	re_printf("num frames:          %zu\n", vds->stats.n_frame);
	re_printf("num headers:         %zu\n", vds->stats.n_header);
	re_printf("key-frames packets:  %zu\n", vds->stats.n_keyframe);
	re_printf("total packets:       %zu\n", vds->stats.n_packet);
}


static void destructor(void *arg)
{
	struct viddec_state *vds = arg;

	if (vds->stats.valid)
		dump_stats(vds);

	if (vds->dec)
		daala_decode_free(vds->dec);

	if (vds->ds)
		daala_setup_free(vds->ds);
	daala_comment_clear(&vds->dc);
	daala_info_clear(&vds->di);
}


int daala_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
			const char *fmtp)
{
	struct viddec_state *vds;
	int err = 0;
	(void)vc;
	(void)fmtp;

	if (!vdsp)
		return EINVAL;

	vds = *vdsp;

	if (vds)
		return 0;

	vds = mem_zalloc(sizeof(*vds), destructor);
	if (!vds)
		return ENOMEM;

	daala_info_init(&vds->di);
	daala_comment_init(&vds->dc);

	if (err)
		mem_deref(vds);
	else
		*vdsp = vds;

	return err;
}


int daala_decode(struct viddec_state *vds, struct vidframe *frame,
		 bool *intra, bool marker, uint16_t seq, struct mbuf *mb)
{
	daala_packet dp;
	bool ishdr;
	int i, r, err = 0;
	(void)seq;

	if (!vds || !frame || !mb)
		return EINVAL;

	*intra = false;

	++vds->stats.n_packet;
	++vds->stats.valid;

	memset(&dp, 0, sizeof(dp));

	dp.packet = mbuf_buf(mb);
	dp.bytes = mbuf_get_left(mb);
	dp.b_o_s = marker;

	ishdr = daala_packet_isheader(&dp);

	if (ishdr)
		++vds->stats.n_header;
	else if (daala_packet_iskeyframe(&dp) > 0) {
		++vds->stats.n_keyframe;
		*intra = true;
	}

	if (daala_packet_isheader(&dp)) {

		r = daala_decode_header_in(&vds->di, &vds->dc, &vds->ds,
					   &dp);
		if (r < 0) {
			warning("daala: decoder: decode_header_in failed"
				" (ret = %d)\n",
				r);
			return EPROTO;
		}
		else if (r == 0) {
			vds->got_headers = true;
			info("daala: all headers received\n");

			vds->dec = daala_decode_create(&vds->di, vds->ds);
			if (!vds->dec) {
				warning("daala: decoder: alloc failed\n");
				return ENOMEM;
			}
		}
		else {
			/* waiting for more headers */
		}
	}
	else {
		daala_image img;

		if (!vds->got_headers) {
			warning("daala: decode: still waiting for headers\n");
			return EPROTO;
		}

		r = daala_decode_packet_in(vds->dec, &dp);
		if (r < 0) {
			warning("daala: decode: packet_in error (%d)\n", r);
			return EPROTO;
		}

		r = daala_decode_img_out(vds->dec, &img);
		if (r != 1) {
			warning("daala: decode: img_out error (%d)\n", r);
			return EPROTO;
		}

		for (i=0; i<3; i++) {
			frame->data[i]     = img.planes[i].data;
			frame->linesize[i] = img.planes[i].ystride;
		}

		frame->size.w = img.width;
		frame->size.h = img.height;
		frame->fmt    = VID_FMT_YUV420P;

		++vds->stats.n_frame;
	}

	return err;
}
