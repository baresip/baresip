/**
 * @file encode.cpp  WebRTC Acoustic Echo Cancellation (AEC) -- Encode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "modules/audio_processing/aec/echo_cancellation.h"
#include "aec.h"


#define SOUND_CARD_BUF 20
#define SKEW           20


struct aec_enc {
	struct aufilt_enc_st af;  /* inheritance */

	struct aec *aec;
	float *buf;
};


static void enc_destructor(void *arg)
{
	struct aec_enc *st = (struct aec_enc *)arg;

	list_unlink(&st->af.le);
	mem_deref(st->aec);
	mem_deref(st->buf);
}


int webrtc_aec_encode_update(struct aufilt_enc_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au)
{
	struct aec_enc *st;
	int err;

	if (!stp || !af)
		return EINVAL;

	if (prm->fmt != AUFMT_FLOAT) {
		warning("webrtc_aec: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (*stp)
		return 0;

	st = (struct aec_enc *)mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	err = webrtc_aec_alloc(&st->aec, ctx, prm);
	if (err)
		goto out;

	st->buf = (float *)mem_zalloc(st->aec->sampc * sizeof(float), NULL);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_enc_st *)st;

	return err;
}


int webrtc_aec_encode(struct aufilt_enc_st *st, void *sampv, size_t *sampc)
{
	struct aec_enc *enc = (struct aec_enc *)st;
	struct aec *aec = enc->aec;
	const float *nearend = (const float *)sampv;
	int err = 0;
	int r;

	if (!st || !sampv || !sampc)
		return EINVAL;

	pthread_mutex_lock(&aec->mutex);

	r = WebRtcAec_Process(aec->inst, &nearend, aec->channels, &enc->buf,
			      *sampc, SOUND_CARD_BUF, SKEW);
	if (r != 0) {
		warning("webrtc_aec: encode: WebRtcAec_Process error (%d)\n",
			r);
		err = EPROTO;
		goto out;
	}

	memcpy(sampv, enc->buf, *sampc * sizeof(float));

 out:
	pthread_mutex_unlock(&aec->mutex);

	return err;
}
