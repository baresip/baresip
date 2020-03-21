/**
 * @file encode.cpp  WebRTC Acoustic Echo Cancellation (AEC) -- Encode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#include "aec.h"


#define SOUND_CARD_BUF 20


struct aec_enc {
	struct aufilt_enc_st af;  /* inheritance */

	struct aec *aec;
	float buf[160];
	enum aufmt fmt;
};


static void enc_destructor(void *arg)
{
	struct aec_enc *st = (struct aec_enc *)arg;

	list_unlink(&st->af.le);
	mem_deref(st->aec);
}


int webrtc_aec_encode_update(struct aufilt_enc_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au)
{
	struct aec_enc *st;
	int err;

	if (!stp || !af || !prm)
		return EINVAL;

	switch (prm->fmt) {

	case AUFMT_S16LE:
	case AUFMT_FLOAT:
		break;

	default:
		warning("webrtc_aec: enc: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (*stp)
		return 0;

	st = (struct aec_enc *)mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->fmt = (enum aufmt)prm->fmt;

	err = webrtc_aec_alloc(&st->aec, ctx, prm);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_enc_st *)st;

	return err;
}


static int encode_float(struct aec_enc *enc, float *sampv, size_t sampc)
{
	struct aec *aec = enc->aec;
	const float *nearend = (const float *)sampv;
	const float *in;
	float *out;
	float *rec = (float *)sampv;
	size_t i;
	int r;
	int err = 0;

	pthread_mutex_lock(&aec->mutex);

	for (i = 0; i < sampc; i += aec->subframe_len) {

		in  = &nearend[i];
		out = enc->buf;

		r = WebRtcAec_Process(aec->inst, &in, aec->num_bands,
				      &out, aec->subframe_len,
				      SOUND_CARD_BUF, 0);
		if (r != 0) {
			warning("webrtc_aec: encode:"
				" WebRtcAec_Process error (%d)\n",
				r);
			err = EPROTO;
			goto out;
		}

		memcpy(&rec[i], out, aec->subframe_len * sizeof(float));
	}

 out:
	pthread_mutex_unlock(&aec->mutex);

	return err;
}


int webrtc_aec_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct aec_enc *enc = (struct aec_enc *)st;
	float *flt;
	int err = 0;

	if (!st || !af)
		return EINVAL;

	switch (enc->fmt) {

	case AUFMT_S16LE:
		/* convert from S16 to FLOAT */
		flt = (float *)mem_alloc(af->sampc * sizeof(float), NULL);
		if (!flt)
			return ENOMEM;

		auconv_from_s16(AUFMT_FLOAT, flt,
				(int16_t *)af->sampv, af->sampc);

		/* process */
		err = encode_float(enc, flt, af->sampc);

		/* convert from FLOAT to S16 */
		auconv_to_s16((int16_t *)af->sampv, AUFMT_FLOAT,
			      flt, af->sampc);

		mem_deref(flt);
		break;

	case AUFMT_FLOAT:
		err = encode_float(enc, (float *)af->sampv, af->sampc);
		break;

	default:
		return ENOTSUP;
	}

	return err;
}
