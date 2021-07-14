/**
 * @file decode_h264.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

#include "comvideo.h"

enum {
	DECODE_MAXSZ = 524288,
};


struct viddec_state {
	bool frag;
	struct mbuf *mb;
	size_t frag_start;
	uint16_t frag_seq;
	bool got_keyframe;

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


static int ffdecode(struct viddec_state *st, struct vidframe *frame)
{
	(void) frame;
	st->mb->pos = 0;

	if (!st->got_keyframe) {
		debug("comvideo: waiting for key frame ..\n");
		return 0;
	}

	gst_appsrc_h264_converter_send_frame(
		st->converter, st->mb->buf,
		st->mb->size,
		st->mb->pos, st->mb->end);

	return 0;
}


int decode_h264(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool marker, uint16_t seq, struct mbuf *src) {
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
	} else if (H264_NALU_FU_A == h264_hdr.type) {
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
	} else {
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

	err = ffdecode(st, frame);
	if (err)
		goto out;

	out:
	mbuf_rewind(st->mb);
	st->frag = false;

	return err;
}


static void dec_destructor(void *arg)
{
	struct viddec_state *st = arg;

	mem_deref(st->mb);

	if (comvideo_codec.client_stream) {
		gst_video_client_stream_stop(comvideo_codec.client_stream);
		g_object_unref(comvideo_codec.client_stream);
		comvideo_codec.client_stream = NULL;
	}

	if (st->converter) {
		gst_object_unref(st->converter);
	}
}


int decode_h264_update(
	struct viddec_state **vdsp,
	const struct vidcodec *vc,
	const char *fmtp)
{

	struct viddec_state *st;
	GstVideoClientStream *stream;
	GstAppsrcH264Converter *converter;

	int err = 0;

	if (!vdsp || !vc)
		return EINVAL;

	if (*vdsp)
		return 0;

	(void) fmtp;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	st->mb = mbuf_alloc(1024);
	if (!st->mb) {
		err = ENOMEM;
		goto out;
	}

	stream = gst_video_client_create_stream(
		comvideo_codec.video_client,
		10,
		"sip");

	converter = gst_appsrc_h264_converter_new(stream);

	st->converter = converter;
	comvideo_codec.client_stream = stream;

	g_object_set(
		stream,
		"enabled", comvideo_codec.disp_enabled,
		NULL);

	out:
	if (err)
		mem_deref(st);
	else
		*vdsp = st;

	return err;
}
