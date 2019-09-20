/**
 * @file aac.h AAC Audio Codec -- internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


// TODO:
#define AAC_SRATE    48000
#define AAC_CHANNELS 1


int aac_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		      struct auenc_param *param, const char *fmtp);
int aac_encode_frm(struct auenc_state *aes, uint8_t *buf, size_t *len,
		   int fmt, const void *sampv, size_t sampc);


int aac_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		      const char *fmtp);
int aac_decode_frm(struct audec_state *ads,
		   int fmt, void *sampv, size_t *sampc,
		   const uint8_t *buf, size_t len);
