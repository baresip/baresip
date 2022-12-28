/**
 * @file decode.cpp  WebRTC Acoustic Echo Cancellation (AEC) -- Decode
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aec.h"


struct aec_dec {
	struct aufilt_dec_st af;  /* inheritance */

	struct aec *aec;
};


static void dec_destructor(void *arg)
{
	struct aec_dec *st = (struct aec_dec *)arg;

	list_unlink(&st->af.le);
	mem_deref(st->aec);
}


int webrtc_aec_decode_update(struct aufilt_dec_st **stp, void **ctx,
			     const struct aufilt *af, struct aufilt_prm *prm,
			     const struct audio *au)
{
	struct aec_dec *st;
	int err;

	if (!stp || !af || !prm)
		return EINVAL;

	switch (prm->fmt) {

	case AUFMT_S16LE:
	case AUFMT_FLOAT:
		break;

	default:
		warning("webrtc_aec: dec: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (*stp)
		return 0;

	st = (struct aec_dec *)mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	err = webrtc_aec_alloc(&st->aec, ctx, prm);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_dec_st *)st;

	return err;
}


static int decode_float(struct aec_dec *dec, float *sampv, size_t sampc)
{
	struct aec *aec = dec->aec;
	webrtc::StreamConfig config(aec->srate, aec->ch, false);
	size_t i;
	int r;
	int err = 0;

	if (sampc % aec->blocksize)
		return EINVAL;

	mtx_lock(&aec->mutex);

	for (i = 0; i < sampc; i += aec->blocksize) {

		const float *src = &sampv[i];
		float *dest = &sampv[i];

		r = aec->inst->ProcessReverseStream(&src, config, config,
						    &dest);
		if (r != 0) {
			warning("webrtc_aec: decode: ProcessReverseStream"
				" error (%d)\n", r);
			err = EPROTO;
			goto out;
		}
	}

 out:
	mtx_unlock(&aec->mutex);

	return err;
}


int webrtc_aec_decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct aec_dec *dec = (struct aec_dec *)st;
	float *flt;
	int err = 0;

	if (!st || !af)
		return EINVAL;

	/* convert samples to float if needed */
	switch (af->fmt) {

	case AUFMT_S16LE:
		flt = (float *)mem_alloc(af->sampc * sizeof(float), NULL);
		if (!flt)
			return ENOMEM;

		auconv_from_s16(AUFMT_FLOAT, flt,
				(int16_t *)af->sampv, af->sampc);
		err = decode_float(dec, flt, af->sampc);
		mem_deref(flt);
		break;

	case AUFMT_FLOAT:
		err = decode_float(dec, (float *)af->sampv, af->sampc);
		break;

	default:
		return ENOTSUP;
	}

	return err;
}
