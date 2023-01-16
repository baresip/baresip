/**
 * @file aec.cpp  WebRTC Acoustic Echo Cancellation (AEC) Mobile Mode
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aec.h"


/**
 * @defgroup webrtc_aecm webrtc_aecm
 *
 * Acoustic Echo Cancellation (AEC) Mobile Mode using WebRTC SDK.
 *
 * This code is experimental.
 *
 * Reference:
 *
 *     https://webrtc.org/native-code/
 */


using namespace webrtc;


static void aec_destructor(void *arg)
{
	struct aec *st = (struct aec *)arg;

	if (st->inst)
		WebRtcAecm_Free(st->inst);

	mtx_destroy(&st->mutex);
}


int webrtc_aecm_alloc(struct aec **stp, void **ctx, struct aufilt_prm *prm)
{
	struct aec *aec;
	int err = 0;
	int r;

	if (!stp || !ctx || !prm)
		return EINVAL;

	if (prm->ch > MAX_CHANNELS) {
		warning("webrtc_aecm: unsupported channels (%u > %u)\n",
			prm->ch, MAX_CHANNELS);
		return ENOTSUP;
	}

	if (*ctx) {
		aec = (struct aec *)*ctx;

		if (prm->srate != aec->srate) {

			warning("webrtc_aecm: srate mismatch\n");
			return ENOTSUP;
		}

		*stp = (struct aec *)mem_ref(*ctx);
		return 0;
	}

	aec = (struct aec *)mem_zalloc(sizeof(*aec), aec_destructor);
	if (!aec)
		return ENOMEM;

	aec->srate = prm->srate;

	err = mtx_init(&aec->mutex, mtx_plain) != thrd_success;
	if (err) {
		err = ENOMEM;
		goto out;
	}

	if (prm->srate > 8000)
		aec->subframe_len = 160;
	else
		aec->subframe_len = 80;

	if (prm->srate > 16000)
		aec->num_bands = prm->srate / 16000;
	else
		aec->num_bands = 1;

	info("webrtc_aecm: creating shared state:"
	     " [%u Hz, %u channels, subframe %u samples, num_bands %d]\n",
	     prm->srate, prm->ch, aec->subframe_len, aec->num_bands);

	aec->inst = WebRtcAecm_Create();
	if (!aec->inst) {
		err = ENODEV;
		goto out;
	}

	r = WebRtcAecm_Init(aec->inst, prm->srate);
	if (r != 0) {
		err = ENODEV;
		goto out;
	}

	aec->config.cngMode       = AecmTrue;
	aec->config.echoMode      = 3;

	/* Sets local configuration modes. */
	r = WebRtcAecm_set_config(aec->inst, aec->config);
	if (r != 0) {
		err = ENODEV;
		goto out;
	}

 out:
	if (err)
		mem_deref(aec);
	else {
		*stp = aec;
		*ctx = aec;
	}

	return err;
}


static struct aufilt webrtc_aec = {
	LE_INIT,
	"webrtc_aecm",
	webrtc_aecm_encode_update,
	webrtc_aecm_encode,
	webrtc_aecm_decode_update,
	webrtc_aecm_decode
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &webrtc_aec);
	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&webrtc_aec);
	return 0;
}


extern "C" const struct mod_export DECL_EXPORTS(webrtc_aecm) = {
	"webrtc_aecm",
	"aufilt",
	module_init,
	module_close
};
