/**
 * @file opus.h Private Opus Interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


struct opus_param {
	opus_int32 srate;
	opus_int32 bitrate;
	opus_int32 stereo;
	opus_int32 cbr;
	opus_int32 inband_fec;
	opus_int32 dtx;
};


/* Encode */
int opus_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		       struct auenc_param *prm, const char *fmtp);
int opus_encode_frm(struct auenc_state *aes,
		    bool *marker, uint8_t *buf, size_t *len,
		    int fmt, const void *sampv, size_t sampc);

extern uint32_t opus_complexity;
extern opus_int32 opus_application;
extern opus_int32 opus_packet_loss;

/* Decode */
int opus_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		       const char *fmtp);
int opus_decode_frm(struct audec_state *ads,
		    int fmt, void *sampv, size_t *sampc,
		    bool marker, const uint8_t *buf, size_t len);
int opus_decode_pkloss(struct audec_state *st,
		       int fmt, void *sampv, size_t *sampc,
		       const uint8_t *buf, size_t len);


/* SDP */
void opus_decode_fmtp(struct opus_param *prm, const char *fmtp);


void opus_mirror_params(const char *fmtp);
