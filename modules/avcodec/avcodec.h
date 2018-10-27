/**
 * @file avcodec.h  Video codecs using libavcodec -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(54, 25, 0)
#define AVCodecID CodecID

#define AV_CODEC_ID_NONE  CODEC_ID_NONE
#define AV_CODEC_ID_H263  CODEC_ID_H263
#define AV_CODEC_ID_H264  CODEC_ID_H264
#define AV_CODEC_ID_MPEG4 CODEC_ID_MPEG4

#endif


#if LIBAVUTIL_VERSION_MAJOR < 52
#define AV_PIX_FMT_YUV420P   PIX_FMT_YUV420P
#define AV_PIX_FMT_YUVJ420P  PIX_FMT_YUVJ420P
#define AV_PIX_FMT_NV12      PIX_FMT_NV12
#define AV_PIX_FMT_YUV444P   PIX_FMT_YUV444P
#endif


extern const uint8_t h264_level_idc;
extern AVCodec *avcodec_h264enc;
extern AVCodec *avcodec_h264dec;


/*
 * Encode
 */

struct videnc_state;

int encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		  struct videnc_param *prm, const char *fmtp,
		  videnc_packet_h *pkth, void *arg);
int encode(struct videnc_state *st, bool update, const struct vidframe *frame,
	   uint64_t timestamp);
#ifdef USE_X264
int encode_x264(struct videnc_state *st, bool update,
		const struct vidframe *frame, uint64_t timestamp);
#endif


/*
 * Decode
 */

struct viddec_state;

int decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		  const char *fmtp);
int decode_h263(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool eof, uint16_t seq, struct mbuf *src);
int decode_h264(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool eof, uint16_t seq, struct mbuf *src);
int decode_mpeg4(struct viddec_state *st, struct vidframe *frame,
		 bool *intra, bool eof, uint16_t seq, struct mbuf *src);


int decode_sdpparam_h264(struct videnc_state *st, const struct pl *name,
			 const struct pl *val);


int avcodec_resolve_codecid(const char *s);
