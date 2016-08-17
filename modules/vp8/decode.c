/**
 * @file vp8/decode.c VP8 Decode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include "vp8.h"


enum {
	DECODE_MAXSZ = 524288,
};


struct hdr {
	unsigned x:1;
	unsigned noref:1;
	unsigned start:1;
	unsigned partid:4;
	/* extension fields */
	unsigned i:1;
	unsigned l:1;
	unsigned t:1;
	unsigned k:1;
	uint16_t picid;
	uint8_t tl0picidx;
	unsigned tid:2;
	unsigned y:1;
	unsigned keyidx:5;
};

struct viddec_state {
	vpx_codec_ctx_t ctx;
	struct mbuf *mb;
	bool ctxup;
	bool started;
	uint16_t seq;
};


static void destructor(void *arg)
{
	struct viddec_state *vds = arg;

	if (vds->ctxup)
		vpx_codec_destroy(&vds->ctx);

	mem_deref(vds->mb);
}


int vp8_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		       const char *fmtp)
{
	struct viddec_state *vds;
	vpx_codec_err_t res;
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

	res = vpx_codec_dec_init(&vds->ctx, &vpx_codec_vp8_dx_algo, NULL, 0);
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

	hdr->x      = v>>7 & 0x1;
	hdr->noref  = v>>5 & 0x1;
	hdr->start  = v>>4 & 0x1;
	hdr->partid = v    & 0x07;

	if (hdr->x) {

		if (mbuf_get_left(mb) < 1)
			return EBADMSG;

		v = mbuf_read_u8(mb);

		hdr->i = v>>7 & 0x1;
		hdr->l = v>>6 & 0x1;
		hdr->t = v>>5 & 0x1;
		hdr->k = v>>4 & 0x1;
	}

	if (hdr->i) {

		if (mbuf_get_left(mb) < 1)
			return EBADMSG;

		v = mbuf_read_u8(mb);

		if (v>>7 & 0x1) {

			if (mbuf_get_left(mb) < 1)
				return EBADMSG;

			hdr->picid  = (v & 0x7f)<<8;
			hdr->picid += mbuf_read_u8(mb);
		}
		else {
			hdr->picid = v & 0x7f;
		}
	}

	if (hdr->l) {

		if (mbuf_get_left(mb) < 1)
			return EBADMSG;

		hdr->tl0picidx = mbuf_read_u8(mb);
	}

	if (hdr->t || hdr->k) {

		if (mbuf_get_left(mb) < 1)
			return EBADMSG;

		v = mbuf_read_u8(mb);

		hdr->tid    = v>>6 & 0x3;
		hdr->y      = v>>5 & 0x1;
		hdr->keyidx = v    & 0x1f;
	}

	return 0;
}


static inline bool is_keyframe(struct mbuf *mb)
{
	if (mbuf_get_left(mb) < 1)
		return false;

	if (mb->buf[mb->pos] & 0x01)
		return false;

	return true;
}


static inline int16_t seq_diff(uint16_t x, uint16_t y)
{
	return (int16_t)(y - x);
}


int vp8_decode(struct viddec_state *vds, struct vidframe *frame,
	       bool *intra, bool marker, uint16_t seq, struct mbuf *mb)
{
	vpx_codec_iter_t iter = NULL;
	vpx_codec_err_t res;
	vpx_image_t *img;
	struct hdr hdr;
	int err, i;

	if (!vds || !frame || !intra || !mb)
		return EINVAL;

	*intra = false;

	err = hdr_decode(&hdr, mb);
	if (err)
		return err;

#if 0
	debug("vp8: header: x=%u noref=%u start=%u partid=%u "
	      "i=%u l=%u t=%u k=%u "
	      "picid=%u tl0picidx=%u tid=%u y=%u keyidx=%u\n",
	      hdr.x, hdr.noref, hdr.start, hdr.partid,
	      hdr.i, hdr.l, hdr.t, hdr.k,
	      hdr.picid, hdr.tl0picidx, hdr.tid, hdr.y, hdr.keyidx);
#endif

	if (hdr.start && hdr.partid == 0) {

		if (is_keyframe(mb))
			*intra = true;

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
			warning("vp8: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}

		return 0;
	}

	res = vpx_codec_decode(&vds->ctx, vds->mb->buf,
			       (unsigned int)vds->mb->end, NULL, 1);
	if (res) {
		debug("vp8: decode error: %s\n", vpx_codec_err_to_string(res));
		err = EPROTO;
		goto out;
	}

	img = vpx_codec_get_frame(&vds->ctx, &iter);
	if (!img) {
		debug("vp8: no picture\n");
		goto out;
	}

	if (img->fmt != VPX_IMG_FMT_I420) {
		warning("vp8: bad pixel format (%i)\n", img->fmt);
		goto out;
	}

	for (i=0; i<4; i++) {
		frame->data[i]     = img->planes[i];
		frame->linesize[i] = img->stride[i];
	}

	frame->size.w = img->d_w;
	frame->size.h = img->d_h;
	frame->fmt    = VID_FMT_YUV420P;

 out:
	mbuf_rewind(vds->mb);
	vds->started = false;

	return err;
}
