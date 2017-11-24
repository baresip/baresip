/**
 * @file daala.h  Experimental video-codec using Daala -- internal api
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */


/* Encode */
int daala_encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
			struct videnc_param *prm, const char *fmtp,
			videnc_packet_h *pkth, void *arg);
int daala_encode(struct videnc_state *ves, bool update,
		 const struct vidframe *frame);


/* Decode */
int daala_decode_update(struct viddec_state **vdsp, const struct vidcodec *vc,
			const char *fmtp);
int daala_decode(struct viddec_state *vds, struct vidframe *frame,
		 bool *intra, bool marker, uint16_t seq, struct mbuf *mb);
