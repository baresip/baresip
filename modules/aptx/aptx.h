/**
 * @file aptx.h aptX Audio Codec -- internal interface
 *
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#define APTX_VARIANT 0 /* 0 = Standard, 1 = HQ */

enum {
	APTX_SRATE = 48000,
	APTX_CHANNELS = 2,
	APTX_VARIANT_HD = 1,
	APTX_VARIANT_STANDARD = 0,
	APTX_WORDSIZE = 3,
};


int aptx_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
                       struct auenc_param *param, const char *fmtp);

int aptx_encode_frm(struct auenc_state *aes,
		    bool *marker, uint8_t *buf, size_t *len,
                    int fmt, const void *sampv, size_t sampc);


int aptx_decode_update(struct audec_state **adsp, const struct aucodec *ac,
                       const char *fmtp);

int aptx_decode_frm(struct audec_state *ads, int fmt, void *sampv,
                    size_t *sampc, bool marker,
		    const uint8_t *buf, size_t len);


int aptx_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt, bool offer,
                  void *arg);

bool aptx_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg);
