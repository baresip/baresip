/**
 * @file vp9.h Private VP9 Interface
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

struct vp9_vidcodec {
	struct vidcodec vc;
	uint32_t max_fs;
};

/* Encode */
int vp9_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, void *arg);
int vp9_encode(struct videnc_state *ves, bool update,
	       const struct vidframe *frame, uint64_t timestamp);


/* Decode */
int vp9_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		      const char *fmtp);
int vp9_decode(struct viddec_state *vds, struct vidframe *frame,
	       bool *intra, bool marker, uint16_t seq, struct mbuf *mb);


/* SDP */
uint32_t vp9_max_fs(const char *fmtp);
int  vp9_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
