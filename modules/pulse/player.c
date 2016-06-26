/**
 * @file pulse/player.c  Pulseaudio sound driver - player
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


struct auplay_st {
	const struct auplay *ap;      /* inheritance */

	pa_simple *s;
	pthread_t thread;
	bool run;
	int16_t *sampv;
	size_t sampc;
	auplay_write_h *wh;
	void *arg;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		debug("pulse: stopping playback thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	if (st->s)
		pa_simple_free(st->s);

	mem_deref(st->sampv);
}


static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;
	const size_t num_bytes = st->sampc * 2;
	int ret, pa_error = 0;

	while (st->run) {

		st->wh(st->sampv, st->sampc, st->arg);

		ret = pa_simple_write(st->s, st->sampv, num_bytes, &pa_error);
		if (ret < 0) {
			warning("pulse: pa_simple_write error (%s)\n",
				pa_strerror(pa_error));
		}
	}

	return NULL;
}


int pulse_player_alloc(struct auplay_st **stp, const struct auplay *ap,
		       struct auplay_prm *prm, const char *device,
		       auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	pa_sample_spec ss;
	pa_buffer_attr attr;
	int err = 0, pa_error = 0;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	debug("pulse: opening player (%u Hz, %d channels, device '%s')\n",
	      prm->srate, prm->ch, device);

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->wh  = wh;
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
	attr.tlength   = (uint32_t)pa_usec_to_bytes(prm->ptime * 1000, &ss);
	attr.prebuf    = (uint32_t)-1;
	attr.minreq    = (uint32_t)-1;
	attr.fragsize  = (uint32_t)-1;

	st->s = pa_simple_new(NULL,
			      "Baresip",
			      PA_STREAM_PLAYBACK,
			      str_isset(device) ? device : 0,
			      "VoIP Playback",
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
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("pulse: playback started\n");

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
