/**
 * @file aec.cpp  WebRTC Acoustic Echo Cancellation (AEC)
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#include "aec.h"


/**
 * @defgroup webrtc_aec webrtc_aec
 *
 * Acoustic Echo Cancellation (AEC) using WebRTC SDK.
 *
 * Configuration options:
 *
 \verbatim
  webrtc_aec_extended_filter {yes,no} # Enable extended_filter
 \endverbatim
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
		delete st->inst;
}


int webrtc_aec_alloc(struct aec **stp, void **ctx, struct aufilt_prm *prm)
{
	struct aec *aec;
	int err = 0;

	if (!stp || !ctx || !prm)
		return EINVAL;

	if (prm->ch > MAX_CHANNELS) {
		warning("webrtc_aec: unsupported channels (%u > %u)\n",
			prm->ch, MAX_CHANNELS);
		return ENOTSUP;
	}

	if (*ctx) {
		aec = (struct aec *)*ctx;

		if (prm->srate != aec->srate) {

			warning("webrtc_aec: srate mismatch\n");
			return ENOTSUP;
		}

		*stp = (struct aec *)mem_ref(*ctx);
		return 0;
	}

	aec = (struct aec *)mem_zalloc(sizeof(*aec), aec_destructor);
	if (!aec)
		return ENOMEM;

	aec->srate = prm->srate;
	aec->ch    = prm->ch;

	pthread_mutex_init(&aec->mutex, NULL);

	// NOTE: excluding channel count
	aec->blocksize  = prm->srate * BLOCKSIZE / 1000;

	info("webrtc_aec: creating shared state:"
	     " [%u Hz, %u channels, blocksize %u samples]\n",
	     prm->srate, prm->ch, aec->blocksize);

	aec->inst = AudioProcessing::Create();
	if (!aec->inst) {
		err = ENODEV;
		goto out;
	}

	// enable different filters here

	aec->inst->echo_cancellation()->enable_drift_compensation(false);
	aec->inst->echo_cancellation()->Enable(true);

	aec->inst->echo_cancellation()->enable_metrics(true);
	aec->inst->echo_cancellation()->enable_delay_logging(true);

	aec->inst->gain_control()->Enable(true);

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
	.le      = LE_INIT,
	.name    = "webrtc_aec",
	.encupdh = webrtc_aec_encode_update,
	.ench    = webrtc_aec_encode,
	.decupdh = webrtc_aec_decode_update,
	.dech    = webrtc_aec_decode
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


extern "C" const struct mod_export DECL_EXPORTS(webrtc_aec) = {
	"webrtc_aec",
	"aufilt",
	module_init,
	module_close
};
