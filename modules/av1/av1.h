/**
 * @file av1.h Private AV1 Interface
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */


/* Encode */
int av1_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, const struct video *vid);
int av1_encode_packet(struct videnc_state *ves, bool update,
		      const struct vidframe *frame, uint64_t timestamp);
int av1_encode_packetize(struct videnc_state *ves,
			 const struct vidpacket *packet);


/* Decode */
int av1_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		      const char *fmtp, const struct video *vid);
int av1_decode(struct viddec_state *vds, struct vidframe *frame,
	       struct viddec_packet *pkt);
