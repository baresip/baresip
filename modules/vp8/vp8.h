/**
 * @file vp8.h Private VP8 Interface
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

struct vp8_vidcodec {
	struct vidcodec vc;
	uint32_t max_fs;
};

/* Encode */
int vp8_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, const struct video *vid);
int vp8_encode(struct videnc_state *ves, bool update,
	       const struct vidframe *frame, uint64_t timestamp);
int vp8_encode_packetize(struct videnc_state *ves,
			 const struct vidpacket *packet);


/* Decode */
int vp8_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		      const char *fmtp, const struct video *vid);
int vp8_decode(struct viddec_state *vds, struct vidframe *frame,
	       struct viddec_packet *pkt);


/* SDP */
uint32_t vp8_max_fs(const char *fmtp);
int  vp8_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
