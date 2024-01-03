/**
 * @file vp9.h Private VP9 Interface
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */

struct vp9_vidcodec {
	struct vidcodec vc;
	uint32_t max_fs;
};

/* Encode */
int vp9_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, const struct video *vid);
int vp9_encode(struct videnc_state *ves, bool update,
	       const struct vidframe *frame, uint64_t timestamp);
int vp9_encode_packetize(struct videnc_state *ves,
			 const struct vidpacket *pkt);


/* Decode */
int vp9_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		      const char *fmtp, const struct video *vid);
int vp9_decode(struct viddec_state *vds, struct vidframe *frame,
	       struct viddec_packet *pkt);


/* SDP */
uint32_t vp9_max_fs(const char *fmtp);
int  vp9_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg);
