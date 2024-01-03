/**
 * @file avcodec.h  Video codecs using libavcodec -- internal API
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */


extern const AVCodec *avcodec_h264enc;
extern const AVCodec *avcodec_h264dec;

extern const AVCodec *avcodec_h265enc;
extern const AVCodec *avcodec_h265dec;

extern AVBufferRef *avcodec_hw_device_ctx;
extern enum AVPixelFormat avcodec_hw_pix_fmt;
extern enum AVHWDeviceType avcodec_hw_type;


/*
 * Encode
 */

struct videnc_state;

int avcodec_encode_update(struct videnc_state **vesp,
			  const struct vidcodec *vc, struct videnc_param *prm,
			  const char *fmtp, videnc_packet_h *pkth,
			  const struct video *vid);
int avcodec_encode(struct videnc_state *st, bool update,
		   const struct vidframe *frame, uint64_t timestamp);
int avcodec_packetize(struct videnc_state *st, const struct vidpacket *packet);


/*
 * Decode
 */

struct viddec_state;

int avcodec_decode_update(struct viddec_state **vdsp,
			  const struct vidcodec *vc, const char *fmtp,
			  const struct video *vid);
int avcodec_decode_h264(struct viddec_state *st, struct vidframe *frame,
			struct viddec_packet *pkt);
int avcodec_decode_h265(struct viddec_state *st, struct vidframe *frame,
			struct viddec_packet *pkt);


int avcodec_resolve_codecid(const char *s);


/*
 * SDP
 */

uint32_t h264_packetization_mode(const char *fmtp);
int avcodec_h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
bool avcodec_h264_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *data);
