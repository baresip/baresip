/**
 * @file encode.cpp  WebRTC Acoustic Echo Cancellation (AEC) -- Encode
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aec.h"


#define SOUND_CARD_BUF 20


struct aec_enc {
	struct aufilt_enc_st af;  /* inheritance */

	struct aec *aec;
	int16_t buf[160];
};


static void enc_destructor(void *arg)
{
	struct aec_enc *st = (struct aec_enc *)arg;

	list_unlink(&st->af.le);
	mem_deref(st->aec);
}


int webrtc_aecm_encode_update(struct aufilt_enc_st **stp, void **ctx,
			      const struct aufilt *af, struct aufilt_prm *prm,
			      const struct audio *au)
{
	struct aec_enc *st;
	int err;
	(void)au;

	if (!stp || !af || !prm)
		return EINVAL;

	switch (prm->fmt) {

	case AUFMT_S16LE:
	case AUFMT_FLOAT:
		break;

	default:
		warning("webrtc_aecm: enc: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (*stp)
		return 0;

	st = (struct aec_enc *)mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	err = webrtc_aecm_alloc(&st->aec, ctx, prm);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_enc_st *)st;

	return err;
}


static int encode_s16(struct aec_enc *enc, int16_t *sampv, size_t sampc)
{
	struct aec *aec = enc->aec;
	const int16_t *nearend = (const int16_t *)sampv;
	const int16_t *in;
	int16_t *out;
	int16_t *rec = (int16_t *)sampv;
	size_t i;
	int r;
	int err = 0;

	mtx_lock(&aec->mutex);

	for (i = 0; i < sampc; i += aec->subframe_len) {

		in  = &nearend[i];
		out = enc->buf;

		r = WebRtcAecm_Process(aec->inst, in, NULL, out,
				       aec->subframe_len,
				       SOUND_CARD_BUF);
		if (r != 0) {
			warning("webrtc_aecm: encode:"
				" WebRtcAecm_Process error (%d)\n",
				r);
			err = EPROTO;
			goto out;
		}

		memcpy(&rec[i], out, aec->subframe_len * sizeof(int16_t));
	}

 out:
	mtx_unlock(&aec->mutex);

	return err;
}


int webrtc_aecm_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct aec_enc *enc = (struct aec_enc *)st;
	int16_t *s16;
	int err = 0;

	if (!st || !af)
		return EINVAL;

	switch (af->fmt) {

	case AUFMT_S16LE:
	        err = encode_s16(enc, (int16_t *)af->sampv, af->sampc);
		break;

	case AUFMT_FLOAT:
		/* convert from FLOAT to S16 */
		s16 = (int16_t *)mem_alloc(af->sampc * sizeof(int16_t), NULL);
		if (!s16)
			return ENOMEM;

		auconv_to_s16(s16, AUFMT_FLOAT, (float *)af->sampv, af->sampc);

		/* process */
		err = encode_s16(enc, s16, af->sampc);

		/* convert from S16 to FLOAT */
		auconv_from_s16(AUFMT_FLOAT, (float *)af->sampv,
				s16, af->sampc);

		mem_deref(s16);
		break;

	default:
		return ENOTSUP;
	}

	return err;
}
