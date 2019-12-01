/**
 * @file aac.h AAC Audio Codec -- internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


enum {
	AAC_BITRATE   = 64000,
	AAC_SRATE     = 48000,
	AAC_CHANNELS  =     1,
};


int aac_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		      struct auenc_param *param, const char *fmtp);
int aac_encode_frm(struct auenc_state *aes,
		   bool *marker, uint8_t *buf, size_t *len,
		   int fmt, const void *sampv, size_t sampc);


int aac_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		      const char *fmtp);
int aac_decode_frm(struct audec_state *ads,
		   int fmt, void *sampv, size_t *sampc,
		   bool marker, const uint8_t *buf, size_t len);


int aac_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
                 bool offer, void *arg);
bool aac_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg);
