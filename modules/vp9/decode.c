/**
 * @file vp9/decode.c VP9 Decode
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include "vp9.h"


enum {
	DECODE_MAXSZ = 524288,
};


struct hdr {
	/* header: */
	unsigned i:1;  /* I: Picture ID (PID) present                */
	unsigned p:1;  /* P: Inter-picture predicted layer frame     */
	unsigned l:1;  /* L: Layer indices present                   */
	unsigned f:1;  /* F: Flexible mode                           */
	unsigned b:1;  /* B: Start of a layer frame                  */
	unsigned e:1;  /* E: End of a layer frame                    */
	unsigned v:1;  /* V: Scalability structure (SS) data present */

	/* extension fields */
	uint16_t picid;
};

struct viddec_state {
	vpx_codec_ctx_t ctx;
	struct mbuf *mb;
	bool ctxup;
	bool started;
	uint16_t seq;

	unsigned n_frames;
	size_t n_bytes;
};


static void destructor(void *arg)
{
	struct viddec_state *vds = arg;

	if (vds->ctxup) {
		debug("vp9: decoder stats: frames=%u, bytes=%zu\n",
		      vds->n_frames, vds->n_bytes);

		vpx_codec_destroy(&vds->ctx);
	}

	mem_deref(vds->mb);
}


int vp9_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
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

	res = vpx_codec_dec_init(&vds->ctx, &vpx_codec_vp9_dx_algo, NULL, 0);
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

	hdr->i      = v>>7 & 0x1;
	hdr->p      = v>>6 & 0x1;
	hdr->l      = v>>5 & 0x1;
	hdr->f      = v>>4 & 0x1;
	hdr->b      = v>>3 & 0x1;
	hdr->e      = v>>2 & 0x1;
	hdr->v      = v>>1 & 0x1;

	if (hdr->p) {
		warning("vp9: decode: P-bit not supported\n");
		return EPROTO;
	}
	if (hdr->l) {
		warning("vp9: decode: L-bit not supported\n");
		return EPROTO;
	}
	if (hdr->f) {
		warning("vp9: decode: F-bit not supported\n");
		return EPROTO;
	}
	if (hdr->v) {
		warning("vp9: decode: V-bit not supported\n");
		return EPROTO;
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

	return 0;
}


static inline bool is_keyframe(const struct mbuf *mb)
{
	vpx_codec_stream_info_t si;
	vpx_codec_err_t ret;

	memset(&si, 0, sizeof(si));
	si.sz = sizeof(si);

	ret = vpx_codec_peek_stream_info(&vpx_codec_vp9_dx_algo,
					 mbuf_buf(mb),
					 (unsigned int)mbuf_get_left(mb), &si);
	if (ret != VPX_CODEC_OK)
		return false;

	return si.is_kf;
}


static inline int16_t seq_diff(uint16_t x, uint16_t y)
{
	return (int16_t)(y - x);
}


int vp9_decode(struct viddec_state *vds, struct vidframe *frame,
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

	vds->n_bytes += mbuf_get_left(mb);

	err = hdr_decode(&hdr, mb);
	if (err)
		return err;

#if 0
	debug("vp9: [%c] header: i=%u start=%u end=%u picid=%u \n",
	      marker ? 'M' : ' ', hdr.i, hdr.b, hdr.e, hdr.picid);
#endif

	if (hdr.b) {

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
			warning("vp9: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}

		return 0;
	}

	res = vpx_codec_decode(&vds->ctx, vds->mb->buf,
			       (unsigned int)vds->mb->end, NULL, 1);
	if (res) {
		debug("vp9: decode error: %s\n", vpx_codec_err_to_string(res));
		err = EPROTO;
		goto out;
	}

	img = vpx_codec_get_frame(&vds->ctx, &iter);
	if (!img) {
		debug("vp9: no picture\n");
		goto out;
	}

	if (img->fmt != VPX_IMG_FMT_I420) {
		warning("vp9: bad pixel format (%i)\n", img->fmt);
		goto out;
	}

	for (i=0; i<4; i++) {
		frame->data[i]     = img->planes[i];
		frame->linesize[i] = img->stride[i];
	}

	frame->size.w = img->d_w;
	frame->size.h = img->d_h;
	frame->fmt    = VID_FMT_YUV420P;

	++vds->n_frames;

 out:
	mbuf_rewind(vds->mb);
	vds->started = false;

	return err;
}
