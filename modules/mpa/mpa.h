/**
 * @file mpa.h Private mpa Interface
 *
 * Copyright (C) 2016 Symonics GmbH
 */


struct mpa_param {
	unsigned samplerate;
	unsigned bitrate;
	unsigned layer;
	enum { AUTO=0, STEREO, JOINT_STEREO, SINGLE_CHANNEL, DUAL_CHANNEL } mode;
};


/* Encode */
int mpa_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		       struct auenc_param *prm, const char *fmtp);
int mpa_encode_frm(struct auenc_state *aes, uint8_t *buf, size_t *len,
		    const int16_t *sampv, size_t sampc);


/* Decode */
int mpa_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		       const char *fmtp);
int mpa_decode_frm(struct audec_state *ads, int16_t *sampv, size_t *sampc,
		    const uint8_t *buf, size_t len);
int mpa_decode_pkloss(struct audec_state *st, int16_t *sampv, size_t *sampc);


/* SDP */
void mpa_decode_fmtp(struct mpa_param *prm, const char *fmtp);
