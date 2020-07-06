/**
 * @file aec.cpp  WebRTC Acoustic Echo Cancellation (AEC)
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif
#include "modules/audio_processing/aec/echo_cancellation.h"
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
		WebRtcAec_Free(st->inst);
}


int webrtc_aec_alloc(struct aec **stp, void **ctx, struct aufilt_prm *prm)
{
	struct conf *conf = conf_cur();
	bool extended_filter = false;
	struct aec *aec;
	int err = 0;
	int r;

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

	pthread_mutex_init(&aec->mutex, NULL);

	if (prm->srate > 8000)
		aec->subframe_len = 160;
	else
		aec->subframe_len = 80;

	if (prm->srate > 16000)
		aec->num_bands = prm->srate / 16000;
	else
		aec->num_bands = 1;

	info("webrtc_aec: creating shared state:"
	     " [%u Hz, %u channels, subframe %u samples, num_bands %d]\n",
	     prm->srate, prm->ch, aec->subframe_len, aec->num_bands);

	aec->inst = WebRtcAec_Create();
	if (!aec->inst) {
		err = ENODEV;
		goto out;
	}

	r = WebRtcAec_Init(aec->inst, prm->srate, prm->srate);
	if (r != 0) {
		err = ENODEV;
		goto out;
	}

	WebRtcAec_enable_delay_agnostic(WebRtcAec_aec_core(aec->inst), 1);

	conf_get_bool(conf, "webrtc_aec_extended_filter", &extended_filter);
	if (extended_filter) {
		info("webrtc_aec: enabling extended_filter\n");
		WebRtcAec_enable_extended_filter(WebRtcAec_aec_core(aec->inst),
						 1);
	}

	aec->config.nlpMode       = kAecNlpModerate;
	aec->config.skewMode      = kAecFalse;
	aec->config.metricsMode   = kAecFalse;
	aec->config.delay_logging = kAecFalse;

	/* Sets local configuration modes. */
	r = WebRtcAec_set_config(aec->inst, aec->config);
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


void webrtc_aec_debug(const struct aec *aec)
{
	int median, std;
	float frac_delay;

	if (!aec)
		return;

	if (WebRtcAec_GetDelayMetrics(aec->inst,
				      &median, &std,
				      &frac_delay) == 0) {

		info("webrtc_aec: delay metrics: median=%d, std=%d, "
		     "fraction of poor delays=%f\n",
		     median, std, frac_delay);
	}
}


static struct aufilt webrtc_aec = {
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
