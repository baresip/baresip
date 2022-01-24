/**
 * @file av1.h Private AV1 Interface
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
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


/* OBU (Open Bitstream Units) */

/*
 * OBU Header
 *
 *     0 1 2 3 4 5 6 7
 *    +-+-+-+-+-+-+-+-+
 *    |F| type  |X|S|-| (REQUIRED)
 *    +-+-+-+-+-+-+-+-+
 */
struct obu_hdr {
	bool f;           /* forbidden      */
	unsigned type:4;  /* type           */
	bool x;           /* extension flag */
	bool s;           /* has size field */
	size_t size;      /* payload size   */
};

int    av1_leb128_encode(struct mbuf *mb, size_t value);
int    av1_leb128_decode(struct mbuf *mb, size_t *value);
int    av1_obu_encode(struct mbuf *mb, uint8_t type, bool has_size,
		      size_t len, const uint8_t *payload);
int    av1_obu_decode(struct obu_hdr *hdr, struct mbuf *mb);
int    av1_obu_print(struct re_printf *pf, const struct obu_hdr *hdr);
