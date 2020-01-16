/**
 * @file avcodec.h  Video codecs using libavcodec -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(52, 20, 100)
#define av_frame_alloc avcodec_alloc_frame
#endif


#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55, 63, 100)
#define avcodec_free_context(ctx)				\
								\
	if (*(ctx)) {						\
		avcodec_close(*(ctx));				\
		av_freep((ctx));				\
	}
#endif


#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 37, 100)
#define av_packet_free(pkt)			\
						\
	if (*(pkt)) {				\
						\
		av_free_packet(*(pkt));		\
		av_freep((pkt));		\
	}
#endif


extern AVCodec *avcodec_h264enc;
extern AVCodec *avcodec_h264dec;

extern AVCodec *avcodec_h265enc;
extern AVCodec *avcodec_h265dec;

#if LIBAVUTIL_VERSION_MAJOR >= 56
extern AVBufferRef *avcodec_hw_device_ctx;
extern enum AVPixelFormat avcodec_hw_pix_fmt;
extern enum AVHWDeviceType avcodec_hw_type;
#endif


/*
 * Encode
 */

struct videnc_state;

int avcodec_encode_update(struct videnc_state **vesp,
			  const struct vidcodec *vc,
			  struct videnc_param *prm, const char *fmtp,
			  videnc_packet_h *pkth, void *arg);
int avcodec_encode(struct videnc_state *st, bool update,
		   const struct vidframe *frame, uint64_t timestamp);


/*
 * Decode
 */

struct viddec_state;

int avcodec_decode_update(struct viddec_state **vdsp,
			  const struct vidcodec *vc, const char *fmtp);
int avcodec_decode_h263(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool eof, uint16_t seq, struct mbuf *src);
int avcodec_decode_h264(struct viddec_state *st, struct vidframe *frame,
		bool *intra, bool eof, uint16_t seq, struct mbuf *src);
int avcodec_decode_h265(struct viddec_state *st, struct vidframe *frame,
			bool *intra, bool eof, uint16_t seq, struct mbuf *src);
int avcodec_decode_mpeg4(struct viddec_state *st, struct vidframe *frame,
		 bool *intra, bool eof, uint16_t seq, struct mbuf *src);


int avcodec_resolve_codecid(const char *s);


/*
 * SDP
 */

uint32_t h264_packetization_mode(const char *fmtp);
int avcodec_h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
bool avcodec_h264_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *data);
