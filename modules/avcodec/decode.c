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


#ifndef AV_INPUT_BUFFER_PADDING_SIZE
#define AV_INPUT_BUFFER_PADDING_SIZE 64
#endif


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

	struct {
		unsigned n_key;
		unsigned n_lost;
	} stats;
};


static void destructor(void *arg)
{
	struct viddec_state *st = arg;

	debug("avcodec: decoder stats"
	      " (keyframes:%u, lost_fragments:%u)\n",
	      st->stats.n_key, st->stats.n_lost);

	mem_deref(st->mb);

	if (st->ctx)
		avcodec_free_context(&st->ctx);

	if (st->pict)
		av_free(st->pict);
}


static enum vidfmt avpixfmt_to_vidfmt(enum AVPixelFormat pix_fmt)
{
	switch (pix_fmt) {

	case AV_PIX_FMT_YUV420P:  return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUVJ420P: return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUV444P:  return VID_FMT_YUV444P;
	case AV_PIX_FMT_NV12:     return VID_FMT_NV12;
	case AV_PIX_FMT_NV21:     return VID_FMT_NV21;
	default:                  return (enum vidfmt)-1;
	}
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


#if LIBAVUTIL_VERSION_MAJOR >= 56
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
	const enum AVPixelFormat *p;
	(void)ctx;

	for (p = pix_fmts; *p != -1; p++) {
		if (*p == avcodec_hw_pix_fmt)
			return *p;
	}

	warning("avcodec: decode: Failed to get HW surface format.\n");

	return AV_PIX_FMT_NONE;
}
#endif


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
	else if (0 == str_casecmp(name, "h265")) {
		st->codec = avcodec_h265dec;
		info("avcodec: h265 decoder activated\n");
	}
	else {
		st->codec = avcodec_find_decoder(codec_id);
		if (!st->codec)
			return ENOENT;
	}

	st->ctx = avcodec_alloc_context3(st->codec);

	/* TODO: If avcodec_h264dec is h264_mediacodec, extradata needs to
           added to context that contains Sequence Parameter Set (SPS) and
	   Picture Parameter Set (PPS), before avcodec_open2() is called.
	*/

	st->pict = av_frame_alloc();

	if (!st->ctx || !st->pict)
		return ENOMEM;

#if LIBAVUTIL_VERSION_MAJOR >= 56
	/* Hardware accelleration */
	if (avcodec_hw_device_ctx) {
		st->ctx->hw_device_ctx = av_buffer_ref(avcodec_hw_device_ctx);
		st->ctx->get_format = get_hw_format;

		info("avcodec: decode: hardware accel enabled (%s)\n",
		     av_hwdevice_get_type_name(avcodec_hw_type));
	}
	else {
		info("avcodec: decode: hardware accel disabled\n");
	}
#endif

	if (avcodec_open2(st->ctx, st->codec, NULL) < 0)
		return ENOENT;

	return 0;
}


int avcodec_decode_update(struct viddec_state **vdsp,
			  const struct vidcodec *vc, const char *fmtp)
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


static int ffdecode(struct viddec_state *st, struct vidframe *frame,
		    bool *intra)
{
	AVFrame *hw_frame = NULL;
	AVPacket avpkt;
	int i, got_picture, ret;
	int err = 0;

#if LIBAVUTIL_VERSION_MAJOR >= 56
	if (st->ctx->hw_device_ctx) {
		hw_frame = av_frame_alloc();
		if (!hw_frame)
			return ENOMEM;
	}
#endif

	err = mbuf_fill(st->mb, 0x00, AV_INPUT_BUFFER_PADDING_SIZE);
	if (err)
		return err;
	st->mb->end -= AV_INPUT_BUFFER_PADDING_SIZE;

	av_init_packet(&avpkt);

	avpkt.data = st->mb->buf;
	avpkt.size = (int)st->mb->end;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(57, 37, 100)

	ret = avcodec_send_packet(st->ctx, &avpkt);
	if (ret < 0) {
		warning("avcodec: avcodec_send_packet error,"
			" packet=%zu bytes, ret=%d (%s)\n",
			st->mb->end, ret, av_err2str(ret));
		err = EBADMSG;
		goto out;
	}

	ret = avcodec_receive_frame(st->ctx, hw_frame ? hw_frame : st->pict);
	if (ret == AVERROR(EAGAIN)) {
		goto out;
	}
	else if (ret < 0) {
		warning("avcodec_receive_frame error ret=%d\n", ret);
		err = EBADMSG;
		goto out;
	}

	got_picture = true;
#else
	ret = avcodec_decode_video2(st->ctx, st->pict, &got_picture, &avpkt);
	if (ret < 0) {
		err = EBADMSG;
		goto out;
	}
#endif

	if (got_picture) {

#if LIBAVUTIL_VERSION_MAJOR >= 56
		if (hw_frame) {
			/* retrieve data from GPU to CPU */
			ret = av_hwframe_transfer_data(st->pict, hw_frame, 0);
			if (ret < 0) {
				warning("avcodec: decode: Error transferring"
					" the data to system memory\n");
				goto out;
			}

			st->pict->key_frame = hw_frame->key_frame;
		}
#endif

		frame->fmt = avpixfmt_to_vidfmt(st->pict->format);
		if (frame->fmt == (enum vidfmt)-1) {
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

		if (st->pict->key_frame) {

			*intra = true;
			st->got_keyframe = true;
			++st->stats.n_key;
		}
	}

 out:
	av_frame_free(&hw_frame);
	return err;
}


int avcodec_decode_h264(struct viddec_state *st, struct vidframe *frame,
			bool *intra, bool marker, uint16_t seq,
			struct mbuf *src)
{
	struct h264_hdr h264_hdr;
	const uint8_t nal_seq[3] = {0, 0, 1};
	int err;

	if (!st || !frame || !intra || !src)
		return EINVAL;

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

	if (h264_hdr.type == H264_NAL_SLICE && !st->got_keyframe) {
		debug("avcodec: decoder waiting for keyframe\n");
		return EPROTO;
	}

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

			/* prepend H.264 NAL start sequence */
			mbuf_write_mem(st->mb, nal_seq, 3);

			/* encode NAL header back to buffer */
			err = h264_hdr_encode(&h264_hdr, st->mb);
			if (err)
				goto out;
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
	else if (H264_NAL_STAP_A == h264_hdr.type) {

		while (mbuf_get_left(src) >= 2) {

			const uint16_t len = ntohs(mbuf_read_u16(src));
			struct h264_hdr lhdr;

			if (mbuf_get_left(src) < len)
				return EBADMSG;

			err = h264_hdr_decode(&lhdr, src);
			if (err)
				return err;

			--src->pos;

			err  = mbuf_write_mem(st->mb, nal_seq, 3);
			err |= mbuf_write_mem(st->mb, mbuf_buf(src), len);
			if (err)
				goto out;

			src->pos += len;
		}
	}
	else {
		warning("avcodec: unknown NAL type %u\n", h264_hdr.type);
		return EBADMSG;
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

	err = ffdecode(st, frame, intra);
	if (err)
		goto out;

 out:
	mbuf_rewind(st->mb);
	st->frag = false;

	return err;
}


int avcodec_decode_mpeg4(struct viddec_state *st, struct vidframe *frame,
		 bool *intra, bool marker, uint16_t seq, struct mbuf *src)
{
	int err;

	if (!src)
		return 0;

	(void)seq;

	*intra = false;

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

	err = ffdecode(st, frame, intra);
	if (err)
		goto out;

 out:
	mbuf_rewind(st->mb);

	return err;
}


int avcodec_decode_h263(struct viddec_state *st, struct vidframe *frame,
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

	if (hdr.i && !st->got_keyframe)
		return EPROTO;

#if 0
	debug(".....[%s seq=%5u ] MODE %s -"
	      " SBIT=%u EBIT=%u I=%s"
	      " (%5u/%5u bytes)\n",
	      marker ? "M" : " ", seq,
	      h263_hdr_mode(&hdr) == H263_MODE_A ? "A" : "B",
	      hdr.sbit, hdr.ebit, hdr.i ? "Inter" : "Intra",
	      mbuf_get_left(src), st->mb->end);
#endif

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

	err = ffdecode(st, frame, intra);
	if (err)
		goto out;

 out:
	mbuf_rewind(st->mb);

	return err;
}


enum {
	H265_FU_HDR_SIZE = 1
};

struct h265_fu {
	unsigned s:1;
	unsigned e:1;
	unsigned type:6;
};


static inline int h265_fu_decode(struct h265_fu *fu, struct mbuf *mb)
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


int avcodec_decode_h265(struct viddec_state *vds, struct vidframe *frame,
		       bool *intra, bool marker, uint16_t seq, struct mbuf *mb)
{
	static const uint8_t nal_seq[3] = {0, 0, 1};
	struct h265_nal hdr;
	int err;

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
	if (hdr.nal_unit_type <= 40) {

		mb->pos -= H265_HDR_SIZE;

		err  = mbuf_write_mem(vds->mb, nal_seq, 3);
		err |= mbuf_write_mem(vds->mb, mbuf_buf(mb),mbuf_get_left(mb));
		if (err)
			goto out;
	}
	else if (H265_NAL_FU == hdr.nal_unit_type) {

		struct h265_fu fu;

		err = h265_fu_decode(&fu, mb);
		if (err)
			return err;

		if (fu.s) {
			if (vds->frag) {
				debug("h265: lost fragments; ignoring NAL\n");
				fragment_rewind(vds);
			}

			vds->frag_start = vds->mb->pos;
			vds->frag = true;

			hdr.nal_unit_type = fu.type;

			err  = mbuf_write_mem(vds->mb, nal_seq, 3);
			err |= h265_nal_encode_mbuf(vds->mb, &hdr);
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
	else if (hdr.nal_unit_type == H265_NAL_AP) {

		while (mbuf_get_left(mb) >= 2) {

			const uint16_t len = ntohs(mbuf_read_u16(mb));

			if (mbuf_get_left(mb) < len)
				return EBADMSG;

			err  = mbuf_write_mem(vds->mb, nal_seq, 3);
			err |= mbuf_write_mem(vds->mb, mbuf_buf(mb), len);
			if (err)
				goto out;

                        mb->pos += len;
		}
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

	err = ffdecode(vds, frame, intra);
	if (err)
		goto out;

 out:
	mbuf_rewind(vds->mb);
	vds->frag = false;

	return err;
}
