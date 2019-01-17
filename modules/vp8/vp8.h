/**
 * @file vp8.h Private VP8 Interface
 *
 * Copyright (C) 2010 Creytiv.com
 */

struct vp8_vidcodec {
	struct vidcodec vc;
	uint32_t max_fs;
};

/* Encode */
int vp8_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, void *arg);
int vp8_encode(struct videnc_state *ves, bool update,
	       const struct vidframe *frame, uint64_t timestamp);


/* Decode */
int vp8_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		      const char *fmtp);
int vp8_decode(struct viddec_state *vds, struct vidframe *frame,
	       bool *intra, bool marker, uint16_t seq, struct mbuf *mb);


/* SDP */
uint32_t vp8_max_fs(const char *fmtp);
int  vp8_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
