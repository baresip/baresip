/**
 * @file avcodec.h  Video codecs using FFmpeg libavcodec -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


#if LIBAVCODEC_VERSION_INT >= ((54<<16)+(25<<8)+0)
#define CodecID AVCodecID
#endif


extern const uint8_t h264_level_idc;


/*
 * Encode
 */

struct videnc_state;

int encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		  struct videnc_param *prm, const char *fmtp);
int encode(struct videnc_state *st, bool update, const struct vidframe *frame,
	   videnc_packet_h *pkth, void *arg);
#ifdef USE_X264
int encode_x264(struct videnc_state *st, bool update,
		const struct vidframe *frame,
		videnc_packet_h *pkth, void *arg);
#endif


/*
 * Decode
 */

struct viddec_state;

int decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		  const char *fmtp);
int decode_h263(struct viddec_state *st, struct vidframe *frame,
		bool eof, uint16_t seq, struct mbuf *src);
int decode_h264(struct viddec_state *st, struct vidframe *frame,
		bool eof, uint16_t seq, struct mbuf *src);
int decode_mpeg4(struct viddec_state *st, struct vidframe *frame,
		 bool eof, uint16_t seq, struct mbuf *src);
int decode_h263_test(struct viddec_state *st, struct vidframe *frame,
		     bool marker, uint16_t seq, struct mbuf *src);


int decode_sdpparam_h264(struct videnc_state *st, const struct pl *name,
			 const struct pl *val);
int h264_packetize(struct mbuf *mb, size_t pktsize,
		   videnc_packet_h *pkth, void *arg);
int h264_decode(struct viddec_state *st, struct mbuf *src);
int h264_nal_send(bool first, bool last,
		  bool marker, uint32_t ihdr, const uint8_t *buf,
		  size_t size, size_t maxsz,
		  videnc_packet_h *pkth, void *arg);


int avcodec_resolve_codecid(const char *s);
