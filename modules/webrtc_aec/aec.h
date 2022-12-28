/**
 * @file aec.h  WebRTC Acoustic Echo Cancellation (AEC) -- internal API
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#define WEBRTC_POSIX 1
#include <webrtc/modules/audio_processing/include/audio_processing.h>
#include <webrtc/system_wrappers/include/trace.h>


#define MAX_CHANNELS         1
#define BLOCKSIZE           10  /* ms */


using namespace webrtc;


struct aec {
	AudioProcessing *inst;
	mtx_t mutex;
	uint32_t srate;
	uint8_t ch;
	uint32_t blocksize;
};


/* Encoder */

int webrtc_aec_encode_update(struct aufilt_enc_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au);
int webrtc_aec_encode(struct aufilt_enc_st *st, struct auframe *af);


/* Decoder */

int webrtc_aec_decode_update(struct aufilt_dec_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au);
int webrtc_aec_decode(struct aufilt_dec_st *st, struct auframe *af);


/* Common */

int  webrtc_aec_alloc(struct aec **stp, void **ctx, struct aufilt_prm *prm);
