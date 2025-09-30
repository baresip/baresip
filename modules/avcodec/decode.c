/**
 * @file avcodec/decode.c  Video codecs using libavcodec -- decoder
 *
 * Copyright (C) 2010 - 2013 Alfred E. Heggestad
 */
#include <re.h>
#include <re_h265.h>
#include <rem.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include "h26x.h"
#include "avcodec.h"
#include "parse/h2645_parse.h"

#ifndef AV_INPUT_BUFFER_PADDING_SIZE
#define AV_INPUT_BUFFER_PADDING_SIZE 64
#endif


enum {
	DECODE_MAXSZ = 524288,
};


struct viddec_state {
	const AVCodec *codec;
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

	bool mc;
	bool open;
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
		av_frame_free(&st->pict);
}


static enum vidfmt avpixfmt_to_vidfmt(enum AVPixelFormat pix_fmt)
{
	switch (pix_fmt) {

	case AV_PIX_FMT_YUV420P:  return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUVJ420P: return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUV444P:  return VID_FMT_YUV444P;
	case AV_PIX_FMT_NV12:     return VID_FMT_NV12;
	case AV_PIX_FMT_NV21:     return VID_FMT_NV21;
	case AV_PIX_FMT_YUV422P:  return VID_FMT_YUV422P;
	default:                  return (enum vidfmt)-1;
	}
}


static inline void fragment_rewind(struct viddec_state *vds)
{
	vds->mb->pos = vds->frag_start;
	vds->mb->end = vds->frag_start;
}


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
	if ((0 == str_cmp(st->codec->name,"h264_mediacodec")
			|| 0 == str_cmp(st->codec->name,"hevc_mediacodec"))
			&& !st->mc) {
		st->mc = true;
		st->open = false;
	}

	if (!st->ctx) {
		st->ctx = avcodec_alloc_context3(st->codec);
	}

	if (!st->pict) {
		st->pict = av_frame_alloc();
	}

	if (!st->ctx || !st->pict)
		return ENOMEM;

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

	if (st->open) {
		if (avcodec_open2(st->ctx, st->codec, NULL) < 0)
			return ENOENT;
	}

	return 0;
}


int avcodec_decode_update(struct viddec_state **vdsp,
			  const struct vidcodec *vc, const char *fmtp,
			  const struct video *vid)
{
	struct viddec_state *st;
	int err = 0;

	if (!vdsp || !vc)
		return EINVAL;

	if (*vdsp)
		return 0;

	(void)fmtp;
	(void)vid;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->mc = false;
	st->open = true;
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
		    struct viddec_packet *pkt,bool full_frame)
{
	AVFrame *hw_frame = NULL;
	AVPacket *avpkt = NULL;
	int i, got_picture, ret;
	int err = 0;

	if (st->ctx->hw_device_ctx) {
		hw_frame = av_frame_alloc();
		if (!hw_frame)
			return ENOMEM;
	}

	if (full_frame) {
		err = mbuf_fill(st->mb, 0x00, AV_INPUT_BUFFER_PADDING_SIZE);
		if (err)
			return err;
		st->mb->end -= AV_INPUT_BUFFER_PADDING_SIZE;

		avpkt = av_packet_alloc();
		if (!avpkt) {
			err = ENOMEM;
			goto out;
		}

		avpkt->data = st->mb->buf;
		avpkt->size = (int)st->mb->end;

		ret = avcodec_send_packet(st->ctx, avpkt);
		if (ret < 0 && ret != AVERROR(EAGAIN)) {
			warning("avcodec: decode: "
			"avcodec_send_packet error,"
			" packet=%zu bytes, ret=%d (%s)\n",
			st->mb->end, ret, av_err2str(ret));
			err = EBADMSG;
			goto out;
		}
		(void) ret;
	}

	ret = avcodec_receive_frame(st->ctx, hw_frame ? hw_frame : st->pict);
	if (ret == AVERROR(EAGAIN)) {
		goto out;
	}
	else if (ret < 0) {
		warning("avcodec: avcodec_receive_frame error ret=%d\n", ret);
		err = EBADMSG;
		goto out;
	}

	got_picture = true;

	if (got_picture) {

		if (hw_frame) {
			av_frame_unref(st->pict); /* cleanup old frame */
			if (hw_frame->format == avcodec_hw_pix_fmt) {
				/* retrieve data from GPU to CPU */
				ret = av_hwframe_transfer_data(st->pict,
					hw_frame, 0);
				if (ret < 0) {
					warning("avcodec: decode: "
						"Error transferring"
						" the data to "
						"system memory\n");
					goto out;
				}
			}
			else {
				av_frame_ref(st->pict, hw_frame);
			}

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)
			st->pict->flags = hw_frame->flags;
#else
			st->pict->key_frame = hw_frame->key_frame;
#endif
		}

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

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)
		if (st->pict->flags & AV_FRAME_FLAG_KEY) {
#else
		if (st->pict->key_frame) {
#endif

			pkt->intra = true;
			st->got_keyframe = true;
			++st->stats.n_key;
		}
	}

 out:
	if (hw_frame)
		av_frame_free(&hw_frame);
	if (avpkt)
		av_packet_free(&avpkt);
	return err;
}


int avcodec_decode_h264(struct viddec_state *st, struct vidframe *frame,
			struct viddec_packet *pkt)
{
	struct h264_nal_header h264_hdr;
	const uint8_t nal_seq[3] = {0, 0, 1};
	int err;

	if (!st || !frame || !pkt || !pkt->mb)
		return EINVAL;

	pkt->intra = false;
	struct mbuf *src = pkt->mb;

	err = h264_nal_header_decode(&h264_hdr, src);
	if (err)
		return err;

#if 0
	re_printf("avcodec: decode: %s %s type=%2d %s  \n",
		  marker ? "[M]" : "   ",
		  h264_is_keyframe(h264_hdr.type) ? "<KEY>" : "     ",
		  h264_hdr.type,
		  h264_nal_unit_name(h264_hdr.type));
#endif

	if (h264_hdr.type == H264_NALU_SLICE && !st->got_keyframe) {
		debug("avcodec: decoder waiting for keyframe\n");
		return EPROTO;
	}

	if (h264_hdr.f) {
		info("avcodec: H264 forbidden bit set!\n");
		return EBADMSG;
	}

	if (st->frag && h264_hdr.type != H264_NALU_FU_A) {
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
	else if (H264_NALU_FU_A == h264_hdr.type) {
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
			err = h264_nal_header_encode(st->mb, &h264_hdr);
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

			if (rtp_seq_diff(st->frag_seq, pkt->hdr->seq) != 1) {
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

		st->frag_seq = pkt->hdr->seq;
	}
	else if (H264_NALU_STAP_A == h264_hdr.type) {

		err = h264_stap_decode_annexb(st->mb, src);
		if (err)
			goto out;
	}
	else {
		warning("avcodec: decode: unknown NAL type %u\n",
			h264_hdr.type);
		return EBADMSG;
	}

	if (!pkt->hdr->m) {

		if (st->mb->end > DECODE_MAXSZ) {
			warning("avcodec: decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}
		/* You need to decode the cache of the previous
		frame as soon as possible to avoid
		accumulation Especially when using mediacodec */
		if (st->open) {
			ffdecode(st, frame, pkt,false);
		}

		return 0;
	}

	if (st->frag) {
		err = EPROTO;
		goto out;
	}

	/*	When using MediaCodec hardware decoding,
		you must set width, height,
		and SPS/PPS parameters before decoding the first frame,
		otherwise decoding will fail.
	 	Here, width, height, and extradata are set
		by parsing the SPS/PPS.
	 	This is only required when using MediaCodec hardware
		decoding—other software decoders do not need this.
	 	Additionally, the extradata format must be:
		0x00 0x00 0x01 sps 0x00 0x00 0x01 pps */
	if (st->mc && !st->open) {
		uint8_t sps_data[256];
		int sps_len;
		uint8_t pps_data[256];
		int pps_len;
		int ret = h264_get_sps_pps(
			st->mb->buf,(int)st->mb->pos,
			sps_data, &sps_len,
			pps_data, &pps_len);
		if (ret) {
			warning("avcodec: decode: "
				"h264_get_sps_pps error %d\n",
				ret);
			goto out;
		}
		ret = h264_decode_sps_with_width_and_height(
			sps_data,sps_len,
			&st->ctx->width,
			&st->ctx->height);
		if (ret) {
			warning("avcodec: decode: "
				"h264_decode_sps_"
				"with_width_and_height error %d\n",
				ret);
			goto out;
		}
		st->ctx->extradata_size = sps_len + pps_len + 6;
		st->ctx->extradata = av_malloc(
			st->ctx->extradata_size +
			AV_INPUT_BUFFER_PADDING_SIZE);
		if (!st->ctx->extradata)
			goto out;
		int offset = 0;
		memcpy(st->ctx->extradata+offset, nal_seq, 3);
		offset+=3;
		memcpy(st->ctx->extradata+offset, sps_data, sps_len);
		offset+=sps_len;
		memcpy(st->ctx->extradata+offset, nal_seq, 3);
		offset+=3;
		memcpy(st->ctx->extradata+offset, pps_data, pps_len);
		st->open = true;
		debug("avcodec: decode: init decoder H264\n");
		init_decoder(st, "H264");
	}

	err = ffdecode(st, frame, pkt,true);
	if (err)
		goto out;

 out:
	mbuf_rewind(st->mb);
	st->frag = false;

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
			struct viddec_packet *pkt)
{
	static const uint8_t nal_seq[3] = {0, 0, 1};
	struct h265_nal hdr;
	int err;

	if (!vds || !frame || !pkt || !pkt->mb)
		return EINVAL;

	pkt->intra = false;
	struct mbuf *mb = pkt->mb;

	if (mbuf_get_left(mb) < H265_HDR_SIZE)
		return EBADMSG;

	err = h265_nal_decode(&hdr, mbuf_buf(mb));
	if (err)
		return err;

	mbuf_advance(mb, H265_HDR_SIZE);

#if 0
	debug("avcodec: h265: decode:  [%s]  %s  type=%2d  %s\n",
	      marker ? "M" : " ",
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

			if (rtp_seq_diff(vds->frag_seq, pkt->hdr->seq) != 1) {
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

		vds->frag_seq = pkt->hdr->seq;
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
		warning("avcodec: unknown H265 NAL type %u (%s) [%zu bytes]\n",
			hdr.nal_unit_type,
			h265_nalunit_name(hdr.nal_unit_type),
			mbuf_get_left(mb));
		return EPROTO;
	}

	if (!pkt->hdr->m) {

		if (vds->mb->end > DECODE_MAXSZ) {
			warning("avcodec: h265 decode buffer size exceeded\n");
			err = ENOMEM;
			goto out;
		}
		/* You need to decode the cache of the previous
		frame as soon as possible to avoid
		accumulation Especially when using mediacodec*/
		if (vds->open) {
			ffdecode(vds, frame, pkt,false);
		}

		return 0;
	}

	if (vds->frag) {
		err = EPROTO;
		goto out;
	}

	/* When using MediaCodec hardware decoding, you must set width, height,
		and SPS/PPS parameters before decoding the first frame,
		otherwise decoding will fail.
		Here, width, height, and extradata are set
		by parsing the SPS/PPS.
		This is only required when using MediaCodec hardware
		decoding—other software decoders do not need this.
		Additionally, the extradata format must be:
		0x00 0x00 0x01 vps 0x00 0x00 0x01 sps 0x00 0x00 0x01 pps */
	if (vds->mc && !vds->open) {
		uint8_t vps_data[256];
		int vps_len;
		uint8_t sps_data[256];
		int sps_len;
		uint8_t pps_data[256];
		int pps_len;
		int ret = h265_get_vps_sps_pps(vds->mb->buf, (int)vds->mb->pos
			,vps_data, &vps_len
			,sps_data, &sps_len
			,pps_data, &pps_len);
		if (ret) {
			warning("avcodec: decode: "
				"h265_get_vps_sps_pps error %d\n", ret);
			goto out;
		}
		ret = h265_decode_sps_with_width_and_height(sps_data, sps_len,
			&vds->ctx->width, &vds->ctx->height);
		if (ret) {
			warning("avcodec: decode: "
				"h265_decode_sps_"
				"with_width_and_height error %d\n", ret);
			goto out;
		}
		vds->ctx->extradata_size =vps_len + sps_len + pps_len + 9;
		vds->ctx->extradata = av_malloc(
			vds->ctx->extradata_size
			+ AV_INPUT_BUFFER_PADDING_SIZE);
		if (!vds->ctx->extradata)
			goto out;
		int offset = 0;
		memcpy(vds->ctx->extradata+offset, nal_seq, 3);
		offset+=3;
		memcpy(vds->ctx->extradata+offset, vps_data, vps_len);
		offset+=vps_len;
		memcpy(vds->ctx->extradata+offset, nal_seq, 3);
		offset+=3;
		memcpy(vds->ctx->extradata+offset, sps_data, sps_len);
		offset+=sps_len;
		memcpy(vds->ctx->extradata+offset, nal_seq, 3);
		offset+=3;
		memcpy(vds->ctx->extradata+offset, pps_data, pps_len);
		vds->open = true;
		debug("avcodec: decode: init decoder H265\n");
		init_decoder(vds, "H265");
	}

	err = ffdecode(vds, frame, pkt,true);
	if (err)
		goto out;

 out:
	mbuf_rewind(vds->mb);
	vds->frag = false;

	return err;
}
