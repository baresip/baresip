/**
 * @file avcodec/decode.c  Video codecs using libavcodec -- decoder
 *
 * Copyright (C) 2010 - 2013 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include "h26x.h"
#include "avcodec.h"


enum {
	DECODE_MAXSZ = 524288,
};


struct viddec_state {
	AVCodec *codec;
	AVCodecContext *ctx;
	AVFrame *pict;
	struct mbuf *mb;
	bool got_keyframe;
	size_t frag_start;
	bool frag;
	uint16_t frag_seq;
	double fps;

	struct {
		unsigned n_key;
		unsigned n_lost;
	} stats;
};


static inline int16_t seq_diff(uint16_t x, uint16_t y)
{
	return (int16_t)(y - x);
}


static inline void fragment_rewind(struct viddec_state *vds)
{
	vds->mb->pos = vds->frag_start;
	vds->mb->end = vds->frag_start;
}


static void destructor(void *arg)
{
	struct viddec_state *st = arg;

	debug("avcodec: decoder stats"
	      " (keyframes:%u, lost_fragments:%u)\n",
	      st->stats.n_key, st->stats.n_lost);

	mem_deref(st->mb);

	if (st->ctx) {
		if (st->ctx->codec)
			avcodec_close(st->ctx);
		av_free(st->ctx);
	}

	if (st->pict)
		av_free(st->pict);
}


static int init_decoder(struct viddec_state *st, const char *name)
{
	enum AVCodecID codec_id;

	codec_id = avcodec_resolve_codecid(name);
	if (codec_id == AV_CODEC_ID_NONE)
		return EINVAL;

	/*
	* Special handling of H.264 decoder
	*/
	if (codec_id == AV_CODEC_ID_H264 && avcodec_h264dec) {
		st->codec = avcodec_h264dec;
		info("avcodec: h264 decoder activated\n");
	}
	else {
		st->codec = avcodec_find_decoder(codec_id);
		if (!st->codec)
			return ENOENT;
	}

	st->ctx = avcodec_alloc_context3(st->codec);

#if LIBAVUTIL_VERSION_INT >= ((52<<16)+(20<<8)+100)
	st->pict = av_frame_alloc();
#else
	st->pict = avcodec_alloc_frame();
#endif

	if (!st->ctx || !st->pict)
		return ENOMEM;

	if (avcodec_open2(st->ctx, st->codec, NULL) < 0)
		return ENOENT;

	return 0;
}


int decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		  const char *fmtp)
{
	struct viddec_state *st;
	int err = 0;

	if (!vdsp || !vc)
		return EINVAL;

	if (*vdsp)
		return 0;

	(void)fmtp;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->mb = mbuf_alloc(1024);
	if (!st->mb) {
		err = ENOMEM;
		goto out;
	}

	err = init_decoder(st, vc->name);
	if (err) {
		warning("avcodec: %s: could not init decoder\n", vc->name);
		goto out;
	}

	debug("avcodec: video decoder %s (%s)\n", vc->name, fmtp);

 out:
	if (err)
		mem_deref(st);
	else
		*vdsp = st;

	return err;
}


static int ffdecode(struct viddec_state *st, struct vidframe *frame)
{
	int i, got_picture, ret;
	int err = 0;

	st->mb->pos = 0;

	if (!st->got_keyframe) {
		debug("avcodec: waiting for key frame ..\n");
		return 0;
	}

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

	do {
		AVPacket avpkt;

		av_init_packet(&avpkt);
		avpkt.data = st->mb->buf;
		avpkt.size = (int)st->mb->end;

		ret = avcodec_send_packet(st->ctx, &avpkt);
		if (ret < 0) {
			warning("avcodec: avcodec_send_packet error,"
				" packet=%zu bytes, ret=%d (%s)\n",
				st->mb->end, ret, av_err2str(ret));
			err = EBADMSG;
			goto out;
		}

		ret = avcodec_receive_frame(st->ctx, st->pict);
		if (ret == AVERROR(EAGAIN)) {
			goto out;
		}
		else if (ret < 0) {
			warning("avcodec_receive_frame error ret=%d\n", ret);
			err = EBADMSG;
			goto out;
		}

		got_picture = true;

	} while (0);
#else
	do {
		AVPacket avpkt;

		av_init_packet(&avpkt);
		avpkt.data = st->mb->buf;
		avpkt.size = (int)st->mb->end;

		ret = avcodec_decode_video2(st->ctx, st->pict,
					    &got_picture, &avpkt);
	} while (0);
#endif

	if (ret < 0) {
		err = EBADMSG;
		goto out;
	}

	if (got_picture) {

		double fps;

		switch (st->pict->format) {

		case AV_PIX_FMT_YUV420P:
		case AV_PIX_FMT_YUVJ420P:
			frame->fmt = VID_FMT_YUV420P;
			break;

		case AV_PIX_FMT_NV12:
			frame->fmt = VID_FMT_NV12;
			break;

		case AV_PIX_FMT_YUV444P:
			frame->fmt = VID_FMT_YUV444P;
			break;

		default:
			warning("avcodec: decode: bad pixel format"
				" (%i) (%s)\n",
				st->pict->format,
				av_get_pix_fmt_name(st->pict->format));
			goto out;
		}

		for (i=0; i<4; i++) {
			frame->data[i]     = st->pict->data[i];
			frame->linesize[i] = st->pict->linesize[i];
		}
		frame->size.w = st->ctx->width;
		frame->size.h = st->ctx->height;

#if LIBAVCODEC_VERSION_INT > AV_VERSION_INT(56, 1, 0)
		/* get the framerate of the decoded bitstream */
		fps = av_q2d(st->ctx->framerate);
		if (st->fps != fps) {
			st->fps = fps;
			debug("avcodec: current decoder framerate"
			      " is %.2f fps\n", fps);
		}
#else
		(void)fps;
#endif
	}

 out:
	return err;
}


int decode_h264(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool marker, uint16_t seq, struct mbuf *src)
{
	struct h264_hdr h264_hdr;
	const uint8_t nal_seq[3] = {0, 0, 1};
	int err;

	*intra = false;

	err = h264_hdr_decode(&h264_hdr, src);
	if (err)
		return err;

#if 0
	re_printf("avcodec: decode: %s %s type=%2d %s  \n",
		  marker ? "[M]" : "   ",
		  h264_is_keyframe(h264_hdr.type) ? "<KEY>" : "     ",
		  h264_hdr.type,
		  h264_nalunit_name(h264_hdr.type));
#endif

	if (h264_hdr.f) {
		info("avcodec: H264 forbidden bit set!\n");
		return EBADMSG;
	}

	if (st->frag && h264_hdr.type != H264_NAL_FU_A) {
		debug("avcodec: lost fragments; discarding previous NAL\n");
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
		err  = mbuf_write_mem(st->mb, nal_seq, 3);

		err |= mbuf_write_mem(st->mb, mbuf_buf(src),
				      mbuf_get_left(src));
		if (err)
			goto out;
	}
	else if (H264_NAL_FU_A == h264_hdr.type) {
		struct h264_fu fu;

		err = h264_fu_hdr_decode(&fu, src);
		if (err)
			return err;
		h264_hdr.type = fu.type;

		if (fu.s) {
			if (st->frag) {
				debug("avcodec: start: lost fragments;"
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
			err = h264_hdr_encode(&h264_hdr, st->mb);
		}
		else {
			if (!st->frag) {
				debug("avcodec: ignoring fragment"
				      " (nal=%u)\n", fu.type);
				++st->stats.n_lost;
				return 0;
			}

			if (seq_diff(st->frag_seq, seq) != 1) {
				debug("avcodec: lost fragments detected\n");
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
	else {
		warning("avcodec: unknown NAL type %u\n", h264_hdr.type);
		return EBADMSG;
	}

	if (*intra) {
		st->got_keyframe = true;
		++st->stats.n_key;
	}

	if (!marker) {

		if (st->mb->end > DECODE_MAXSZ) {
			warning("avcodec: decode buffer size exceeded\n");
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


int decode_mpeg4(struct viddec_state *st, struct vidframe *frame,
		 bool *intra, bool marker, uint16_t seq, struct mbuf *src)
{
	int err;

	if (!src)
		return 0;

	(void)seq;

	*intra = false;

	/* let the decoder handle this */
	st->got_keyframe = true;

	err = mbuf_write_mem(st->mb, mbuf_buf(src),
			     mbuf_get_left(src));
	if (err)
		goto out;

	if (!marker) {

		if (st->mb->end > DECODE_MAXSZ) {
			warning("avcodec: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}

		return 0;
	}

	if (st->mb->end >= 5) {

		/* 0 == I-frame
		 * 1 == P-frame
		 */
		uint8_t pict_type = (st->mb->buf[4] & 0xc0) >> 6;

		if (pict_type == I_FRAME) {
			*intra = true;
		}
	}

	err = ffdecode(st, frame);
	if (err)
		goto out;

 out:
	mbuf_rewind(st->mb);

	return err;
}


int decode_h263(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool marker, uint16_t seq, struct mbuf *src)
{
	struct h263_hdr hdr;
	int err;

	if (!st || !frame || !intra)
		return EINVAL;

	*intra = false;

	if (!src)
		return 0;

	(void)seq;

	err = h263_hdr_decode(&hdr, src);
	if (err)
		return err;

#if 0
	debug(".....[%s seq=%5u ] MODE %s -"
	      " SBIT=%u EBIT=%u I=%s"
	      " (%5u/%5u bytes)\n",
	      marker ? "M" : " ", seq,
	      h263_hdr_mode(&hdr) == H263_MODE_A ? "A" : "B",
	      hdr.sbit, hdr.ebit, hdr.i ? "Inter" : "Intra",
	      mbuf_get_left(src), st->mb->end);
#endif

	if (!hdr.i) {
		st->got_keyframe = true;
		if (st->mb->pos == 0) {
			*intra = true;
		}
	}

#if 0
	if (st->mb->pos == 0) {
		uint8_t *p = mbuf_buf(src);

		if (p[0] != 0x00 || p[1] != 0x00) {
			warning("invalid PSC detected (%02x %02x)\n",
				p[0], p[1]);
			return EPROTO;
		}
	}
#endif

	/*
	 * The H.263 Bit-stream can be fragmented on bit-level,
	 * indicated by SBIT and EBIT. Example:
	 *
	 *               8 bit  2 bit
	 *            .--------.--.
	 * Packet 1   |        |  |
	 * SBIT=0     '--------'--'
	 * EBIT=6
	 *                        .------.--------.--------.
	 * Packet 2               |      |        |        |
	 * SBIT=2                 '------'--------'--------'
	 * EBIT=0                   6bit    8bit     8bit
	 *
	 */

	if (hdr.sbit > 0) {
		const uint8_t mask  = (1 << (8 - hdr.sbit)) - 1;
		const uint8_t sbyte = mbuf_read_u8(src) & mask;

		st->mb->buf[st->mb->end - 1] |= sbyte;
	}

	err = mbuf_write_mem(st->mb, mbuf_buf(src),
			     mbuf_get_left(src));
	if (err)
		goto out;

	if (!marker) {

		if (st->mb->end > DECODE_MAXSZ) {
			warning("avcodec: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}

		return 0;
	}

	if (!hdr.i) {
		++st->stats.n_key;
	}

	err = ffdecode(st, frame);
	if (err)
		goto out;

 out:
	mbuf_rewind(st->mb);

	return err;
}
