/**
 * @file mpa.h Private mpa Interface
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#define MPA_FRAMESIZE 1152
#define MPA_IORATE 48000
#define MPA_RTPRATE 90000
#define BARESIP_FRAMESIZE (MPA_IORATE/50*2)

#undef DEBUG

struct mpa_param {
	unsigned samplerate;
	unsigned bitrate;
	unsigned layer;
	enum { AUTO=0, STEREO, JOINT_STEREO, SINGLE_CHANNEL, DUAL_CHANNEL }
		mode;
};


/* Encode */
int mpa_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		       struct auenc_param *prm, const char *fmtp);
int mpa_encode_frm(struct auenc_state *aes, uint8_t *buf, size_t *len,
		   int fmt, const void *sampv, size_t sampc);


/* Decode */
int mpa_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		       const char *fmtp);
int mpa_decode_frm(struct audec_state *ads,
		   int fmt, void *sampv, size_t *sampc,
		   const uint8_t *buf, size_t len);

/* SDP */
void mpa_decode_fmtp(struct mpa_param *prm, const char *fmtp);
