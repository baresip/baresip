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
	void *sampv;
	size_t sampc;
	size_t sampsz;
	auplay_write_h *wh;
	void *arg;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;
	int pa_error = 0;
	int pa_ret;

	/* Wait for termination of other thread */
	if (st->run) {
		debug("pulse: stopping playback thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	if (st->s) {
		pa_ret = pa_simple_drain(st->s, &pa_error);
		if (pa_ret < 0)
			warning("pulse: pa_simple_drain error (%s)\n",
				 pa_strerror(pa_error));

		pa_simple_free(st->s);
	}

	mem_deref(st->sampv);
}


static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;
	const size_t num_bytes = st->sampc * st->sampsz;
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


static int aufmt_to_pulse_format(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return PA_SAMPLE_S16NE;
	case AUFMT_FLOAT:  return PA_SAMPLE_FLOAT32NE;
	default: return 0;
	}
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
	st->sampsz = aufmt_sample_size(prm->fmt);

	st->sampv = mem_alloc(st->sampsz * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	ss.format   = aufmt_to_pulse_format(prm->fmt);
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
			      str_isset(device) ? device : NULL,
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


static void dev_list_cb(pa_context *c, const pa_sink_info *l, int eol,
			void *userdata)
{
	struct list *dev_list = userdata;
	int err;
	(void)c;

	if (eol > 0) {
		return;
	}

	err = mediadev_add(dev_list, l->name);

	if (err) {
		warning("pulse player: media device (%s) can not be added\n",
			l->name);
	}
}


static pa_operation *get_dev_info(pa_context *pa_ctx, struct list *dev_list){

	return pa_context_get_sink_info_list(pa_ctx, dev_list_cb,
						dev_list);
}


int pulse_player_init(struct auplay *ap)
{
	if (!ap) {
		return EINVAL;
	}

	list_init(&ap->dev_list);

	return set_available_devices(&ap->dev_list, get_dev_info);
}
