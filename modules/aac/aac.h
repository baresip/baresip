/**
 * @file aac.h MPEG-4 AAC Audio Codec -- internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2019 Hessischer Rundfunk
 */


struct aac_param {
	uint32_t profile_level_id;
	uint32_t sizelength;
	uint32_t indexlength;
	uint32_t indexdeltalength;
	char config[64];
	char mode[8];
	uint32_t constantduration;
	uint32_t bitrate;
};


enum {
	AU_HDR_LEN = 4, /* single access unit only!!! */

	AAC_SIZELENGTH = 13,
	AAC_INDEXLENGTH = 3,
	AAC_INDEXDELTALENGTH = 3,
	AAC_STREAMTYPE_AUDIO = 5,

	HIGH_QUALITY_AUDIO_PROFILE = 16,       /* L3 */
	LOW_DELAY_AUDIO_PROFILE = 25,          /* L4 */
	ENHANCED_LOW_DELAY_AUDIO_PROFILE = 76, /* L1 */
	HIGH_EFFICIENCY_AAC_PROFILE = 46,      /* L4 */
	HIGH_EFFICIENCY_AAC_V2_PROFILE = 49,   /* L3 */
	AAC_PROFILE = 41,                      /* L2 */
};

extern uint32_t aac_samplerate, aac_channels, aac_aot;
extern uint32_t aac_bitrate, aac_profile, aac_constantduration;


int aac_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
                      struct auenc_param *param, const char *fmtp);
int aac_encode_frm(struct auenc_state *aes, bool *marker, uint8_t *buf,
                   size_t *len, int fmt, const void *sampv, size_t sampc);


int aac_decode_update(struct audec_state **adsp, const struct aucodec *ac,
                      const char *fmtp);
int aac_decode_frm(struct audec_state *ads, int fmt, void *sampv,
                   size_t *sampc, bool marker, const uint8_t *buf,
                   size_t len);


int aac_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt, bool offer,
                 void *arg);
bool aac_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg);


void aac_encode_fmtp(const struct aac_param *prm);

void aac_decode_fmtp(struct aac_param *prm, const char *fmtp);


void aac_mirror_params(const char *fmtp);
