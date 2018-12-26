/**
 * @file decode.cpp  WebRTC Acoustic Echo Cancellation (AEC) -- Decode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "modules/audio_processing/aec/echo_cancellation.h"
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

	if (!stp || !af)
		return EINVAL;

	if (prm->fmt != AUFMT_FLOAT) {
		warning("webrtc_aec: unsupported sample format (%s)\n",
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


int webrtc_aec_decode(struct aufilt_dec_st *st, void *sampv, size_t *sampc)
{
	struct aec_dec *dec = (struct aec_dec *)st;
	struct aec *aec = dec->aec;
	const float *farend = (const float *)sampv;
	int r;
	int err = 0;
	size_t i;

	if (!st || !sampv || !sampc)
		return EINVAL;

	pthread_mutex_lock(&aec->mutex);

	for (i = 0; i < *sampc; i += aec->subframe_len) {

		r = WebRtcAec_BufferFarend(aec->inst, farend + i,
					   aec->subframe_len);
		if (r != 0) {
			warning("webrtc_aec: decode: WebRtcAec_BufferFarend"
				" error (%d)\n", r);
			err = EPROTO;
			goto out;
		}
	}

 out:
	pthread_mutex_unlock(&aec->mutex);

	return err;
}
