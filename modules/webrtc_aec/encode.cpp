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


static AudioProcessing::ChannelLayout get_layout(uint8_t ch)
{
	switch (ch) {

	case 1: return AudioProcessing::kMono;
	case 2: return AudioProcessing::kStereo;
	default: return (AudioProcessing::ChannelLayout)-1;
	}
}


static int encode_float(struct aec_enc *enc, float *sampv, size_t sampc)
{
	struct aec *aec = enc->aec;
	size_t i;
	int r;
	int err = 0;

	if (sampc % aec->blocksize)
		return EINVAL;

	mtx_lock(&aec->mutex);

	for (i = 0; i < sampc; i += aec->blocksize) {

		size_t samples_per_channel = aec->blocksize / aec->ch;
		const float *src = &sampv[i];
		float *dest = &sampv[i];

		// NOTE: important
		aec->inst->set_stream_delay_ms(SOUND_CARD_BUF);

		r = aec->inst->ProcessStream(&src,
					     samples_per_channel,
					     aec->srate,
					     get_layout(aec->ch),
					     aec->srate,
					     get_layout(aec->ch),
					     &dest);
		if (r != 0) {
			warning("webrtc_aec: encode:"
				" ProcessStream error (%d)\n",
				r);
			err = EPROTO;
			goto out;
		}
	}

 out:
	mtx_unlock(&aec->mutex);

	return err;
}


int webrtc_aec_encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct aec_enc *enc = (struct aec_enc *)st;
	float *flt;
	int err = 0;

	if (!st || !af)
		return EINVAL;

	switch (af->fmt) {

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
