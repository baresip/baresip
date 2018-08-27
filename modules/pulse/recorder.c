/**
 * @file pulse/recorder.c  Pulseaudio sound driver - recorder
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <pthread.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "pulse.h"


struct ausrc_st {
	const struct ausrc *as;      /* inheritance */

	pa_simple *s;
	pthread_t thread;
	bool run;
	void *sampv;
	size_t sampc;
	size_t sampsz;
	uint32_t ptime;
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
	const size_t num_bytes = st->sampc * st->sampsz;
	int ret, pa_error = 0;
	uint64_t now, last_read, diff;
	unsigned dropped = 0;
	bool init = true;

	if (pa_simple_flush(st->s, &pa_error)) {
		warning("pulse: pa_simple_flush error (%s)\n",
		        pa_strerror(pa_error));
	}

	last_read = tmr_jiffies();

	while (st->run) {

		ret = pa_simple_read(st->s, st->sampv, num_bytes, &pa_error);
		if (ret < 0) {
			warning("pulse: pa_simple_write error (%s)\n",
				pa_strerror(pa_error));
			continue;
		}

		/* Some devices might send a burst of samples right after the
		   initialization - filter them out */
		if (init) {
			now = tmr_jiffies();
			diff = (now > last_read)? now - last_read : 0;

			if (diff < st->ptime / 2) {
				last_read = now;
				++dropped;
				continue;
			}
			else {
				init = false;

				if (dropped)
					debug("pulse: dropped %u frames of "
					      "garbage at the beginning of "
					      "the recording\n", dropped);
			}
		}

		st->rh(st->sampv, st->sampc, st->arg);
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


int pulse_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *device,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct mediadev *md;
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
	st->sampsz = aufmt_sample_size(prm->fmt);
	st->ptime = prm->ptime;

	st->sampv = mem_alloc(st->sampsz * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	ss.format   = aufmt_to_pulse_format(prm->fmt);
	ss.channels = prm->ch;
	ss.rate     = prm->srate;

	attr.maxlength = (uint32_t)-1;
	attr.tlength   = (uint32_t)-1;
	attr.prebuf    = (uint32_t)-1;
	attr.minreq    = (uint32_t)-1;
	attr.fragsize  = (uint32_t)pa_usec_to_bytes(prm->ptime * 1000, &ss);

	md = mediadev_get_default(&as->dev_list);

	st->s = pa_simple_new(NULL,
			      "Baresip",
			      PA_STREAM_RECORD,
			      str_isset(device) ? device : md->name,
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


static void dev_list_cb(pa_context *c, const pa_source_info *l,
						int eol, void *userdata)
{
	struct list *dev_list = userdata;
	int err;

	if (eol > 0) {
		return;
	}

	/* In pulseaudio every sink automatically has a monitor source
	This "output" device must be filtered out */
	if(!strstr(l->name,"output")) {
		err = mediadev_add(dev_list, l->name);
		if (err) {
			warning("pulse recorder: media device (%s) "
					"can not be added\n",l->name);
		}
	}
}


static int set_available_devices(struct ausrc *as)
{
	int err;
	pa_mainloop *pa_ml;
	pa_mainloop_api *pa_mlapi;
	pa_operation *pa_op;
	pa_context *pa_ctx;

	/* Create a mainloop API and connection to the default server */
	pa_ml = pa_mainloop_new();
	pa_mlapi = pa_mainloop_get_api(pa_ml);
	pa_ctx = pa_context_new(pa_mlapi, "Baresip");

	pa_context_connect(pa_ctx, NULL, 0, NULL);

	while(pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) {
		err = pa_mainloop_iterate(pa_ml, 1, NULL);
		if (err < 0) {
			return err;
		}
	}

	pa_op = pa_context_get_source_info_list(pa_ctx, dev_list_cb,
						&as->dev_list);

	while (pa_operation_get_state(pa_op) != PA_OPERATION_DONE) {
		err = pa_mainloop_iterate(pa_ml, 1, NULL);
		if (err < 0) {
			return err;
		}
	}

	pa_operation_unref(pa_op);
	pa_context_disconnect(pa_ctx);
	pa_context_unref(pa_ctx);
	pa_mainloop_free(pa_ml);

	return 0;
}


int pulse_recorder_init(struct ausrc *as)
{
	list_init(&as->dev_list);

	return set_available_devices(as);
}

