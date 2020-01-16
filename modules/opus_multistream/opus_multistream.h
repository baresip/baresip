/**
 * @file opus_multistream.h Private Opus Multistream Interface
 *
 * Copyright (C) 2019 Creytiv.com
 */


struct opus_multistream_param {
	opus_int32 srate;
	opus_int32 bitrate;
	opus_int32 stereo;
	opus_int32 cbr;
	opus_int32 inband_fec;
	opus_int32 dtx;
};


/* Encode */
int opus_multistream_encode_update(struct auenc_state **aesp,
				   const struct aucodec *ac,
		       struct auenc_param *prm, const char *fmtp);
int opus_multistream_encode_frm(struct auenc_state *aes,
				bool *marker, uint8_t *buf, size_t *len,
		    int fmt, const void *sampv, size_t sampc);

extern uint32_t opus_ms_complexity;
extern opus_int32 opus_ms_application;
extern uint32_t opus_ms_streams;
extern uint32_t opus_ms_c_streams;

/* Decode */
int opus_multistream_decode_update(struct audec_state **adsp,
				   const struct aucodec *ac,
		       const char *fmtp);
int opus_multistream_decode_frm(struct audec_state *ads,
		    int fmt, void *sampv, size_t *sampc,
				bool marker, const uint8_t *buf, size_t len);
int opus_multistream_decode_pkloss(struct audec_state *st,
				   int fmt, void *sampv, size_t *sampc,
				   const uint8_t *buf, size_t len);


/* SDP */
void opus_multistream_decode_fmtp(struct opus_multistream_param *prm,
				  const char *fmtp);


void opus_multistream_mirror_params(const char *fmtp);
