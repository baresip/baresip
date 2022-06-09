/**
 * @file decode_h264.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

#include "comvideo.h"
#include <re_h264.h>

enum {
	DECODE_MAXSZ = 524288,
};


struct viddec_state {
	bool frag;
	struct mbuf *mb;
	size_t frag_start;
	uint16_t frag_seq;
	bool got_keyframe;

	unsigned int width;
	unsigned int height;

	GstAppsrcH264Converter *converter;

	struct {
		unsigned n_key;
		unsigned n_lost;
	} stats;
};


static inline int16_t
seq_diff(uint16_t x, uint16_t y) {
	return (int16_t)(y - x);
}


static inline void fragment_rewind(struct viddec_state *vds) {
	vds->mb->pos = vds->frag_start;
	vds->mb->end = vds->frag_start;
}


static int h264_convert(struct viddec_state *st, struct vidframe *frame)
{
	(void) frame;
	st->mb->pos = 0;

	if (!st->got_keyframe) {
		debug("comvideo: waiting for key frame ..\n");
		return 0;
	}

	frame->data[0] = st->mb->buf;
	frame->linesize[0] = st->mb->end;
	frame->fmt = VID_FMT_N;
	frame->size.h = st->height;
	frame->size.w = st->width;

	return 0;
}


static void
handle_h264_size(struct viddec_state *st, struct mbuf *src)
{
	struct h264_sps sps;
	struct vidsz sz;
	int res;

	res = h264_sps_decode(&sps, src->buf + src->pos, src->end - src->pos);
	if(res) {
		warning("comvideo: Could not decode SPS");
		return;
	}

	debug("idc: %x%x \n", sps.profile_idc, sps.level_idc);

	h264_sps_resolution(&sps, &sz.w, &sz.h);
	debug("size %d x %d \n", sz.w, sz.h);

	st->width = sz.w;
	st->height = sz.h;
}


int decode_h264(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool marker, uint16_t seq, struct mbuf *src)
{
	struct h264_nal_header h264_hdr;
	const uint8_t nal_seq[3] = {0, 0, 1};
	int err;

	*intra = false;

	err = h264_nal_header_decode(&h264_hdr, src);
	if (err)
		return err;

	if (h264_hdr.f) {
		info("comvideo: H264 forbidden bit set!\n");
		return EBADMSG;
	}

	if (st->frag && h264_hdr.type != H264_NALU_FU_A) {
		debug("comvideo: lost fragments; discarding previous NAL\n");
		fragment_rewind(st);
		st->frag = false;
		++st->stats.n_lost;
	}

	/* handle NAL types */

	if(H264_NALU_SPS == h264_hdr.type) {
		handle_h264_size(st, src);

	}

	if (1 <= h264_hdr.type && h264_hdr.type <= 23) {

		if (h264_is_keyframe(h264_hdr.type))
			*intra = true;

		--src->pos;

		/* prepend H.264 NAL start sequence */
		err = mbuf_write_mem(st->mb, nal_seq, 3);

		err |= mbuf_write_mem(st->mb, mbuf_buf(src),
				      mbuf_get_left(src));
		if (err)
			goto out;
	}
	else if (H264_NALU_FU_A == h264_hdr.type) {
		struct h264_fu fu;

		err = h264_fu_hdr_decode(&fu, src);
		if (err)
			return err;
		h264_hdr.type = fu.type;

		if (fu.s) {
			if (st->frag) {
				debug("comvideo: start: lost fragments;"
				      " ignoring previous NAL\n");
				fragment_rewind(st);
				++st->stats.n_lost;
			}

			st->frag_start = st->mb->pos;
			st->frag = true;

			if (h264_is_keyframe(fu.type))
				*intra = true;

			/* prepend H.264 NAL start sequence */
			mbuf_write_mem(st->mb, nal_seq, 3);

			/* encode NAL header back to buffer */
			err = h264_nal_header_encode(st->mb, &h264_hdr);
		} else {
			if (!st->frag) {
				debug("comvideo: ignoring fragment"
				      " (nal=%u)\n", fu.type);
				++st->stats.n_lost;
				return 0;
			}

			if (seq_diff(st->frag_seq, seq) != 1) {
				debug("comvideo: lost fragments detected\n");
				fragment_rewind(st);
				st->frag = false;
				++st->stats.n_lost;
				return 0;
			}
		}

		err = mbuf_write_mem(st->mb, mbuf_buf(src),
				     mbuf_get_left(src));
		if (err)
			goto out;

		if (fu.e)
			st->frag = false;

		st->frag_seq = seq;
	}
	else if (H264_NALU_STAP_A == h264_hdr.type) {

		while (mbuf_get_left(src) >= 2) {

			const uint16_t len = ntohs(mbuf_read_u16(src));
			struct h264_nal_header lhdr;

			if (mbuf_get_left(src) < len)
				return EBADMSG;

			err = h264_nal_header_decode(&lhdr, src);
			if (err)
				return err;

			--src->pos;

			err = mbuf_write_mem(st->mb, nal_seq, 3);
			err |= mbuf_write_mem(st->mb, mbuf_buf(src), len);
			if (err)
				goto out;

			src->pos += len;
		}
	}
 	else {
		warning("comvideo: unknown NAL type %u\n", h264_hdr.type);
		return EBADMSG;
	}

	if (*intra) {
		st->got_keyframe = true;
		++st->stats.n_key;
	}

	if (!marker) {
		if (st->mb->end > DECODE_MAXSZ) {
			warning("comvideo: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}
		return 0;
	}

	if (st->frag) {
		err = EPROTO;
		goto out;
	}

	err = h264_convert(st, frame);

	out:
	mbuf_rewind(st->mb);
	st->frag = false;

	return err;
}


static void dec_destructor(void *arg)
{
	struct viddec_state *st = arg;
	mem_deref(st->mb);
}


int decode_h264_update(
	struct viddec_state **vdsp,
	const struct vidcodec *vc,
	const char *fmtp)
{
	struct viddec_state *st;

	int err = 0;

	if (!vdsp || !vc)
		return EINVAL;

	if (*vdsp)
		return 0;

	(void) fmtp;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->width = 0;
	st->height = 0;
	st->mb = mbuf_alloc(1024);
	if (!st->mb) {
		err = ENOMEM;
		goto out;
	}

	out:
	if (err)
		mem_deref(st);
	else
		*vdsp = st;

	return err;
}
