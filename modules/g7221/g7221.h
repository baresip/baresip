/**
 * @file g7221.h Private G.722.1 Interface
 *
 * Copyright (C) 2010 Creytiv.com
 */

struct g7221_aucodec {
	struct aucodec ac;
	uint32_t bitrate;
};

/* Encode */
int g7221_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
			struct auenc_param *prm, const char *fmtp);
int g7221_encode(struct auenc_state *aes, uint8_t *buf, size_t *len,
		 int fmt, const void *sampv, size_t sampc);


/* Decode */
int g7221_decode_update(struct audec_state **adsp, const struct aucodec *ac,
			const char *fmtp);
int g7221_decode(struct audec_state *ads,
		 int fmt, void *sampv, size_t *sampc,
		 const uint8_t *buf, size_t len);


/* SDP */
int  g7221_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		   bool offer, void *arg);
bool g7221_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg);
