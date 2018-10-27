/**
 * @file h265/decode.c H.265 Decode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavutil/pixdesc.h>
#include <libavcodec/avcodec.h>
#include "h265.h"


#if LIBAVUTIL_VERSION_MAJOR < 52
#define AV_PIX_FMT_YUV420P PIX_FMT_YUV420P
#endif


enum {
	FU_HDR_SIZE = 1
};

enum {
	DECODE_MAXSZ = 524288,
};


struct fu {
	unsigned s:1;
	unsigned e:1;
	unsigned type:6;
};

struct viddec_state {
	AVCodecContext *ctx;
	AVFrame *pict;
	struct mbuf *mb;
	size_t frag_start;
	bool frag;
	uint16_t frag_seq;
};


static void destructor(void *arg)
{
	struct viddec_state *vds = arg;

	if (vds->ctx) {
		avcodec_close(vds->ctx);
		av_free(vds->ctx);
	}

	if (vds->pict)
		av_free(vds->pict);

	mem_deref(vds->mb);
}


int h265_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		       const char *fmtp)
{
	struct viddec_state *vds;
	AVCodec *codec;
	int err = 0;
	(void)vc;
	(void)fmtp;

	if (!vdsp)
		return EINVAL;

	vds = *vdsp;

	if (vds)
		return 0;

	/* HEVC = H.265 */
	codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
	if (!codec) {
		warning("h265: could not find H265 decoder\n");
		return ENOSYS;
	}

	vds = mem_zalloc(sizeof(*vds), destructor);
	if (!vds)
		return ENOMEM;

	vds->mb = mbuf_alloc(1024);
	if (!vds->mb) {
		err = ENOMEM;
		goto out;
	}

	vds->pict = av_frame_alloc();
	if (!vds->pict) {
		err = ENOMEM;
		goto out;
	}

	vds->ctx = avcodec_alloc_context3(codec);
	if (!vds->ctx) {
		err = ENOMEM;
		goto out;
	}

	if (avcodec_open2(vds->ctx, codec, NULL) < 0) {
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(vds);
	else
		*vdsp = vds;

	return err;
}


static inline int fu_decode(struct fu *fu, struct mbuf *mb)
{
	uint8_t v;

	if (mbuf_get_left(mb) < 1)
		return EBADMSG;

	v = mbuf_read_u8(mb);

	fu->s    = v>>7 & 0x1;
	fu->e    = v>>6 & 0x1;
	fu->type = v>>0 & 0x3f;

	return 0;
}


static inline int16_t seq_diff(uint16_t x, uint16_t y)
{
	return (int16_t)(y - x);
}


static inline void fragment_rewind(struct viddec_state *vds)
{
	vds->mb->pos = vds->frag_start;
	vds->mb->end = vds->frag_start;
}


int h265_decode(struct viddec_state *vds, struct vidframe *frame,
		bool *intra, bool marker, uint16_t seq, struct mbuf *mb)
{
	static const uint8_t nal_seq[3] = {0, 0, 1};
	int err, ret, got_picture, i;
	struct h265_nal hdr;
	AVPacket avpkt;
	enum vidfmt fmt;

	if (!vds || !frame || !intra || !mb)
		return EINVAL;

	*intra = false;

	err = h265_nal_decode(&hdr, mbuf_buf(mb));
	if (err)
		return err;

	mbuf_advance(mb, H265_HDR_SIZE);

#if 0
	debug("h265: decode: %s type=%2d  %s\n",
		  h265_is_keyframe(hdr.nal_unit_type) ? "<KEY>" : "     ",
		  hdr.nal_unit_type,
		  h265_nalunit_name(hdr.nal_unit_type));
#endif

	if (vds->frag && hdr.nal_unit_type != H265_NAL_FU) {
		debug("h265: lost fragments; discarding previous NAL\n");
		fragment_rewind(vds);
		vds->frag = false;
	}

	/* handle NAL types */
	if (0 <= hdr.nal_unit_type && hdr.nal_unit_type <= 40) {

		if (h265_is_keyframe(hdr.nal_unit_type))
			*intra = true;

		mb->pos -= H265_HDR_SIZE;

		err  = mbuf_write_mem(vds->mb, nal_seq, 3);
		err |= mbuf_write_mem(vds->mb, mbuf_buf(mb),mbuf_get_left(mb));
		if (err)
			goto out;
	}
	else if (H265_NAL_FU == hdr.nal_unit_type) {

		struct fu fu;

		err = fu_decode(&fu, mb);
		if (err)
			return err;

		if (fu.s) {
			if (h265_is_keyframe(fu.type))
				*intra = true;

			if (vds->frag) {
				debug("h265: lost fragments; ignoring NAL\n");
				fragment_rewind(vds);
			}

			vds->frag_start = vds->mb->pos;
			vds->frag = true;

			hdr.nal_unit_type = fu.type;

			err  = mbuf_write_mem(vds->mb, nal_seq, 3);
			err = h265_nal_encode_mbuf(vds->mb, &hdr);
			if (err)
				goto out;
		}
		else {
			if (!vds->frag) {
				debug("h265: ignoring fragment\n");
				return 0;
			}

			if (seq_diff(vds->frag_seq, seq) != 1) {
				debug("h265: lost fragments detected\n");
				fragment_rewind(vds);
				vds->frag = false;
				return 0;
			}
		}

		err = mbuf_write_mem(vds->mb, mbuf_buf(mb), mbuf_get_left(mb));
		if (err)
			goto out;

		if (fu.e)
			vds->frag = false;

		vds->frag_seq = seq;
	}
	else {
		warning("h265: unknown NAL type %u (%s) [%zu bytes]\n",
			hdr.nal_unit_type,
			h265_nalunit_name(hdr.nal_unit_type),
			mbuf_get_left(mb));
		return EPROTO;
	}

	if (!marker) {

		if (vds->mb->end > DECODE_MAXSZ) {
			warning("h265: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}

		return 0;
	}

	if (vds->frag) {
		err = EPROTO;
		goto out;
	}

	av_init_packet(&avpkt);
	avpkt.data = vds->mb->buf;
	avpkt.size = (int)vds->mb->end;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

	ret = avcodec_send_packet(vds->ctx, &avpkt);
	if (ret < 0) {
		err = EBADMSG;
		goto out;
	}

	ret = avcodec_receive_frame(vds->ctx, vds->pict);
	if (ret < 0) {
		err = EBADMSG;
		goto out;
	}

	got_picture = true;

#else
	ret = avcodec_decode_video2(vds->ctx, vds->pict, &got_picture, &avpkt);
	if (ret < 0) {
		debug("h265: decode error\n");
		err = EPROTO;
		goto out;
	}
#endif

	if (!got_picture) {
		/* debug("h265: no picture\n"); */
		goto out;
	}

	switch (vds->pict->format) {

	case AV_PIX_FMT_YUV420P:
		fmt = VID_FMT_YUV420P;
		break;

	case AV_PIX_FMT_YUV444P:
		fmt = VID_FMT_YUV444P;
		break;

	default:
		warning("h265: decode: bad pixel format (%i) (%s)\n",
			vds->pict->format,
			av_get_pix_fmt_name(vds->pict->format));
		goto out;
	}

	for (i=0; i<4; i++) {
		frame->data[i]     = vds->pict->data[i];
		frame->linesize[i] = vds->pict->linesize[i];
	}

	frame->size.w = vds->ctx->width;
	frame->size.h = vds->ctx->height;
	frame->fmt    = fmt;

 out:
	mbuf_rewind(vds->mb);
	vds->frag = false;

	return err;
}
