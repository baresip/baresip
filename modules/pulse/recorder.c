/**
 * @file pulse/recorder.c  Pulseaudio sound driver - recorder
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "pulse.h"


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */

	pa_simple *s;
	pthread_t thread;
	bool run;
	int16_t *sampv;
	size_t sampc;
	ausrc_read_h *rh;
	void *arg;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		debug("pulse: stopping record thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	if (st->s)
		pa_simple_free(st->s);

	mem_deref(st->sampv);
}


static void *read_thread(void *arg)
{
	struct ausrc_st *st = arg;
	const size_t num_bytes = st->sampc * 2;
	int ret, pa_error = 0;

	while (st->run) {

		ret = pa_simple_read(st->s, st->sampv, num_bytes, &pa_error);
		if (ret < 0) {
			warning("pulse: pa_simple_write error (%s)\n",
				pa_strerror(pa_error));
			continue;
		}

		st->rh(st->sampv, st->sampc, st->arg);
	}

	return NULL;
}


int pulse_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *device,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	pa_sample_spec ss;
	pa_buffer_attr attr;
	int pa_error;
	int err;

	(void)ctx;
	(void)device;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	debug("pulse: opening recorder (%u Hz, %d channels, device '%s')\n",
	      prm->srate, prm->ch, device);

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->sampv = mem_alloc(2 * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	ss.format   = PA_SAMPLE_S16NE;
	ss.channels = prm->ch;
	ss.rate     = prm->srate;

	attr.maxlength = (uint32_t)-1;
	attr.tlength   = (uint32_t)-1;
	attr.prebuf    = (uint32_t)-1;
	attr.minreq    = (uint32_t)-1;
	attr.fragsize  = (uint32_t)pa_usec_to_bytes(prm->ptime * 1000, &ss);

	st->s = pa_simple_new(NULL,
			      "Baresip",
			      PA_STREAM_RECORD,
			      str_isset(device) ? device : 0,
			      "VoIP Record",
			      &ss,
			      NULL,
			      &attr,
			      &pa_error);
	if (!st->s) {
		warning("pulse: could not connect to server (%s)\n",
			pa_strerror(pa_error));
		err = ENODEV;
		goto out;
	}

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("pulse: recording started\n");

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
