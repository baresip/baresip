/**
 * @file av1.h Private AV1 Interface
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */


/* Encode */
int av1_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
		      struct videnc_param *prm, const char *fmtp,
		      videnc_packet_h *pkth, void *arg);
int av1_encode_packet(struct videnc_state *ves, bool update,
		      const struct vidframe *frame, uint64_t timestamp);


/* Decode */
int av1_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
		      const char *fmtp);
int av1_decode(struct viddec_state *vds, struct vidframe *frame,
	       bool *intra, bool marker, uint16_t seq, struct mbuf *mb);
