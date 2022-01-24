/**
 * @file av1/decode.c AV1 Decode
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <aom/aom.h>
#include <aom/aom_decoder.h>
#include <aom/aomdx.h>
#include "av1.h"


enum {
	DECODE_MAXSZ = 524288,
};


/** AV1 Aggregation Header */
struct hdr {
	unsigned z:1;  /* continuation of an OBU fragment from prev packet  */
	unsigned y:1;  /* last OBU element will continue in the next packet */
	unsigned w:2;  /* number of OBU elements in the packet              */
	unsigned n:1;  /* first packet of a coded video sequence            */
};

struct viddec_state {
	aom_codec_ctx_t ctx;
	struct mbuf *mb;
	struct mbuf *mb_dec;
	bool ctxup;
	bool started;
	uint16_t seq;
	unsigned w;
};


static void destructor(void *arg)
{
	struct viddec_state *vds = arg;

	if (vds->ctxup)
		aom_codec_destroy(&vds->ctx);

	mem_deref(vds->mb);
}


int av1_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		       const char *fmtp)
{
	struct viddec_state *vds;
	aom_codec_dec_cfg_t cfg = {
		.allow_lowbitdepth = 1
	};
	aom_codec_err_t res;
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

	vds->mb = mbuf_alloc(1024);
	if (!vds->mb) {
		err = ENOMEM;
		goto out;
	}

	res = aom_codec_dec_init(&vds->ctx, &aom_codec_av1_dx_algo, &cfg, 0);
	if (res) {
		err = ENOMEM;
		goto out;
	}

	vds->ctxup = true;

 out:
	if (err)
		mem_deref(vds);
	else
		*vdsp = vds;

	return err;
}


static inline int hdr_decode(struct hdr *hdr, struct mbuf *mb)
{
	uint8_t v;

	memset(hdr, 0, sizeof(*hdr));

	if (mbuf_get_left(mb) < 1)
		return EBADMSG;

	v = mbuf_read_u8(mb);

	hdr->z = v>>7 & 0x1;
	hdr->y = v>>6 & 0x1;
	hdr->w = v>>4 & 0x3;
	hdr->n = v>>3 & 0x1;

	return 0;
}


static inline int16_t seq_diff(uint16_t x, uint16_t y)
{
	return (int16_t)(y - x);
}


static int copy_obu(struct mbuf *mb, const uint8_t *frag, size_t len)
{
	struct mbuf mbf = { (uint8_t *)frag, len, 0, len };
	struct obu_hdr hdr;
	int err;

	err = av1_obu_decode(&hdr, &mbf);
	if (err) {
		warning("av1: could not decode OBU: %m\n", err);
		return err;
	}

	switch (hdr.type) {

	case OBU_TEMPORAL_DELIMITER:
	case OBU_TILE_GROUP:
	case OBU_PADDING:
		/* MUST be ignored by receivers. */
		warning("av1: decode: unexpected obu type %u\n", hdr.type);
		return EPROTO;

	default:
		break;
	}

	err = av1_obu_encode(mb, hdr.type, true,
			     hdr.size, mbuf_buf(&mbf));
	if (err)
		return err;

	return 0;
}


int av1_decode(struct viddec_state *vds, struct vidframe *frame,
	       bool *intra, bool marker, uint16_t seq, struct mbuf *mb)
{
	aom_codec_frame_flags_t flags;
	aom_codec_iter_t iter = NULL;
	aom_codec_err_t res;
	aom_image_t *img;
	struct hdr hdr;
	size_t size;
	int err, i;

	if (!vds || !frame || !intra || !mb)
		return EINVAL;

	*intra = false;

	err = hdr_decode(&hdr, mb);
	if (err)
		return err;

#if 1
	debug("av1: decode: header:  [%s]  [seq=%u, %4zu bytes]"
	      "  z=%u  y=%u  w=%u  n=%u\n",
	      marker ? "M" : " ",
	      seq, mbuf_get_left(mb),
	      hdr.z, hdr.y, hdr.w, hdr.n);
#endif

	if (!hdr.z) {

		/* save the W obu count */
		vds->w = hdr.w;

		mbuf_rewind(vds->mb);
		vds->started = true;
	}
	else {
		if (!vds->started)
			return 0;

		if (seq_diff(vds->seq, seq) != 1) {
			mbuf_rewind(vds->mb);
			vds->started = false;
			return 0;
		}
	}

	vds->seq = seq;

	err = mbuf_write_mem(vds->mb, mbuf_buf(mb), mbuf_get_left(mb));
	if (err)
		goto out;

	if (!marker) {

		if (vds->mb->end > DECODE_MAXSZ) {
			warning("av1: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}

		return 0;
	}

	vds->mb->pos = 0;

	if (!vds->mb_dec)
		vds->mb_dec = mbuf_alloc(1024);

	/* prepend Temporal Delimiter */
	err = av1_obu_encode(vds->mb_dec, OBU_TEMPORAL_DELIMITER,
			     true, 0, NULL);
	if (err)
		goto out;

	switch (vds->w) {

	case 3:
		err = av1_leb128_decode(vds->mb, &size);
		if (err)
			goto out;

		err = copy_obu(vds->mb_dec, mbuf_buf(vds->mb), size);
		if (err)
			goto out;

		mbuf_advance(vds->mb, size);

		/*@fallthrough@*/
	case 2:
		err = av1_leb128_decode(vds->mb, &size);
		if (err)
			goto out;

		err = copy_obu(vds->mb_dec, mbuf_buf(vds->mb), size);
		if (err)
			goto out;

		mbuf_advance(vds->mb, size);

		/*@fallthrough@*/
	case 1:
		size = vds->mb->end - vds->mb->pos;

		err = copy_obu(vds->mb_dec, mbuf_buf(vds->mb), size);
		if (err)
			goto out;
		break;

	case 0:
		while (mbuf_get_left(vds->mb) >= 2) {

			err = av1_leb128_decode(vds->mb, &size);
			if (err)
				goto out;

			if (size > mbuf_get_left(vds->mb)) {
				warning("av1: short packet (%zu > %zu)\n",
					size, mbuf_get_left(vds->mb));
				err = EPROTO;
				goto out;
			}

			mbuf_advance(vds->mb, size);

			err = copy_obu(vds->mb_dec, mbuf_buf(vds->mb),
				       size);
			if (err)
				goto out;
		}
		break;
	}

	res = aom_codec_decode(&vds->ctx, vds->mb_dec->buf,
			       (unsigned int)vds->mb_dec->end, NULL);
	if (res) {
		warning("av1: decode error [w=%u, %zu bytes]: %s (%s)\n",
			hdr.w,
			vds->mb_dec->end,
			aom_codec_err_to_string(res),
			aom_codec_error_detail(&vds->ctx));
		err = EPROTO;
		goto out;
	}

	img = aom_codec_get_frame(&vds->ctx, &iter);
	if (!img) {
		debug("av1: no picture\n");
		goto out;
	}

	res = aom_codec_control(&vds->ctx, AOMD_GET_FRAME_FLAGS, &flags);
	if (res == AOM_CODEC_OK) {
		if (flags & AOM_FRAME_IS_KEY)
			*intra = true;
	}

	if (img->fmt != AOM_IMG_FMT_I420) {
		warning("av1: bad pixel format (%i)\n", img->fmt);
		goto out;
	}

	for (i=0; i<3; i++) {
		frame->data[i]     = img->planes[i];
		frame->linesize[i] = img->stride[i];
	}

	frame->size.w = img->d_w;
	frame->size.h = img->d_h;
	frame->fmt    = VID_FMT_YUV420P;

 out:
	mbuf_rewind(vds->mb);
	vds->started = false;

	vds->mb_dec = mem_deref(vds->mb_dec);

	return err;
}
