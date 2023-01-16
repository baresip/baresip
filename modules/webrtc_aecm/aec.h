/**
 * @file aec.h  WebRTC Acoustic Echo Cancellation (AEC) -- internal API
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */


#include "modules/audio_processing/aecm/echo_control_mobile.h"


#define MAX_CHANNELS         1


using namespace webrtc;


struct aec {
	AecmConfig config;
	void *inst;
	mtx_t mutex;
	uint32_t srate;
	uint32_t subframe_len;
	int num_bands;
};


/* Encoder */

int webrtc_aecm_encode_update(struct aufilt_enc_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au);
int webrtc_aecm_encode(struct aufilt_enc_st *st, struct auframe *af);


/* Decoder */

int webrtc_aecm_decode_update(struct aufilt_dec_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au);
int webrtc_aecm_decode(struct aufilt_dec_st *st, struct auframe *af);


/* Common */

int  webrtc_aecm_alloc(struct aec **stp, void **ctx, struct aufilt_prm *prm);
