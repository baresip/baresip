/**
 * @file /srtp/sdes.h  SDP Security Descriptions for Media Streams API
 *
 * Copyright (C) 2010 Creytiv.com
 */


struct crypto {
	uint32_t tag;
	struct pl suite;
	struct pl key_method;
	struct pl key_info;
	struct pl lifetime;  /* optional */
	struct pl mki;       /* optional */
	struct pl sess_prms; /* optional */
};

extern const char sdp_attr_crypto[];

int sdes_encode_crypto(struct sdp_media *m, uint32_t tag, const char *suite,
		       const char *key, size_t key_len);
int sdes_decode_crypto(struct crypto *c, const char *val);
