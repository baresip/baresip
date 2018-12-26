/**
 * @file aec.h  WebRTC Acoustic Echo Cancellation (AEC) -- internal API
 *
 * Copyright (C) 2010 Creytiv.com
 */


#include "modules/audio_processing/aec/echo_cancellation.h"


#define MAX_SAMPLE_RATE  16000
#define MAX_CHANNELS         1


using namespace webrtc;


struct aec {
	AecConfig config;
	void *inst;
	pthread_mutex_t mutex;
	uint32_t srate;
	uint32_t subframe_len;
};


/* Encoder */

int webrtc_aec_encode_update(struct aufilt_enc_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au);
int webrtc_aec_encode(struct aufilt_enc_st *st, void *sampv, size_t *sampc);


/* Decoder */

int webrtc_aec_decode_update(struct aufilt_dec_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au);
int webrtc_aec_decode(struct aufilt_dec_st *st, void *sampv, size_t *sampc);


/* Common */

int  webrtc_aec_alloc(struct aec **stp, void **ctx, struct aufilt_prm *prm);
void webrtc_aec_debug(const struct aec *aec);
