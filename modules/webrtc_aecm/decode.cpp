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


int webrtc_aecm_decode_update(struct aufilt_dec_st **stp, void **ctx,
			      const struct aufilt *af, struct aufilt_prm *prm,
			      const struct audio *au)
{
	struct aec_dec *st;
	int err;
	(void)au;

	if (!stp || !af || !prm)
		return EINVAL;

	switch (prm->fmt) {

	case AUFMT_S16LE:
	case AUFMT_FLOAT:
		break;

	default:
		warning("webrtc_aecm: dec: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (*stp)
		return 0;

	st = (struct aec_dec *)mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	err = webrtc_aecm_alloc(&st->aec, ctx, prm);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_dec_st *)st;

	return err;
}


static int decode_s16(struct aec_dec *dec, const int16_t *sampv, size_t sampc)
{
	struct aec *aec = dec->aec;
	const int16_t *farend = sampv;
	size_t i;
	int r;
	int err = 0;

	mtx_lock(&aec->mutex);

	for (i = 0; i < sampc; i += aec->subframe_len) {

		r = WebRtcAecm_BufferFarend(aec->inst, farend + i,
					    aec->subframe_len);
		if (r != 0) {
			warning("webrtc_aecm: decode: WebRtcAecm_BufferFarend"
				" error (%d)\n", r);
			err = EPROTO;
			goto out;
		}
	}

 out:
	mtx_unlock(&aec->mutex);

	return err;
}


int webrtc_aecm_decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct aec_dec *dec = (struct aec_dec *)st;
	int16_t *s16;
	int err = 0;

	if (!st || !af)
		return EINVAL;

	/* convert samples to float if needed */
	switch (af->fmt) {

	case AUFMT_S16LE:
		err = decode_s16(dec, (int16_t *)af->sampv, af->sampc);
		break;

	case AUFMT_FLOAT:
		s16 = (int16_t *)mem_alloc(af->sampc * sizeof(int16_t), NULL);
		if (!s16)
			return ENOMEM;

		auconv_to_s16(s16, AUFMT_FLOAT,	(float *)af->sampv,
			      af->sampc);
		err = decode_s16(dec, s16, af->sampc);
		mem_deref(s16);
		break;

	default:
		return ENOTSUP;
	}

	return err;
}
