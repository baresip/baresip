/**
 * @file pulse.c  Pulseaudio sound driver (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com
 *                                  c.spielberger@commend.com
 *                                  c.huber@commend.com
 */

#include <string.h>
#include <pulse/pulseaudio.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "pulse.h"


/**
 * @defgroup pulse pulse
 *
 * Audio driver module for Pulseaudio
 *
 * This module is using the pulseaudio async interface.
 */

enum {
	RECONN_DELAY = 1500,
};

static struct auplay *auplay;
static struct ausrc *ausrc;


struct pa {
	struct tmr rc;
	struct mqueue *q;
	uint8_t retry;
	struct paconn_st *paconn;
};


static struct pa pa;


static int paconn_start(struct paconn_st **ppaconn);


static void reconnth(void *arg)
{
	(void) arg;

	++pa.retry;

	tmr_cancel(&pa.rc);
	if (pa.paconn)
		pa.paconn = mem_deref(pa.paconn);

	if (paconn_start(&pa.paconn) == 0)
		return;

	if (pa.retry < 10)
		tmr_start(&pa.rc, RECONN_DELAY, reconnth, NULL);
	else
		warning ("pulse: could not connect to pulseaudio\n");
}


static void qh(int id, void *data, void *arg)
{
	(void) id;
	(void) data;
	(void) arg;

	if (pa.paconn)
		pa.paconn = mem_deref(pa.paconn);

	pa.retry = 0;
	tmr_start(&pa.rc, RECONN_DELAY, reconnth, NULL);
}


/**
 * context state callback, gets called by the libpulse
 *
 * @param context pulseaudio context object
 * @param arg     Pulseaudio connection object
 */
static void context_state_cb(pa_context *context, void *arg)
{
	struct paconn_st *c = arg;

	switch (pa_context_get_state(context)) {
		case PA_CONTEXT_FAILED:
			pa_threaded_mainloop_signal(c->mainloop, 0);
			mqueue_push(pa.q, 0, NULL);
			break;

		case PA_CONTEXT_READY:
			pa_threaded_mainloop_signal(c->mainloop, 0);
			pulse_player_init(auplay);
			pulse_recorder_init(ausrc);
			break;

		case PA_CONTEXT_TERMINATED:
			pa_threaded_mainloop_signal(c->mainloop, 0);
			break;

		case PA_CONTEXT_UNCONNECTED:
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
			break;
	}
}


/**
 * Pulseaudio Connection object destructor
 *
 * @param arg paconn object
 */
static void paconn_destructor(void *arg)
{
	struct paconn_st *paconn = arg;

	if (paconn->mainloop)
		pa_threaded_mainloop_stop(paconn->mainloop);

	if (paconn->context) {
		pa_context_disconnect(paconn->context);
		pa_context_unref(paconn->context);
		paconn->context = NULL;
	}

	if (paconn->mainloop) {
		pa_threaded_mainloop_free(paconn->mainloop);
		paconn->mainloop = NULL;
	}

	if (auplay)
		list_flush(&auplay->dev_list);

	if (ausrc)
		list_flush(&ausrc->dev_list);
}


/**
 * Init Pulseaudio connection object
 *
 * @param ppaconn_st paconn pointer
 *
 * @return int 0 if success, errorcode otherwise
 */
static int paconn_start(struct paconn_st **ppaconn)
{
	int err = 0;
	struct paconn_st *c;

	if (!ppaconn)
		return EINVAL;

	c = mem_zalloc(sizeof(*c), paconn_destructor);
	if (!c)
		return ENOMEM;

	c->mainloop = pa_threaded_mainloop_new();
	if (!c->mainloop)
		return ENOMEM;

	c->context = pa_context_new(pa_threaded_mainloop_get_api(c->mainloop),
		"baresip");
	if (!c->context) {
		err = ENOMEM;
		goto out;
	}

	pa_context_set_state_callback(c->context, context_state_cb, c);
	if (pa_context_connect(c->context, NULL, 0, NULL) < 0) {
		warning ("pulse: could not connect to context (%s)\n",
			pa_strerror(pa_context_errno(c->context)));
		err = EINVAL;
		goto out;
	}

	pa_threaded_mainloop_lock(c->mainloop);
	if (pa_threaded_mainloop_start(c->mainloop) < 0)
		err = EINVAL;

	pa_threaded_mainloop_unlock(c->mainloop);
	info ("pulse: initialized (%m)\n", err);

  out:
	if (err)
		mem_deref(c);
	else
		*ppaconn = c;

	return err;
}


/**
 * Init Pulseaudio async object
 *
 * @return int 0 if success, errorcode otherwise
 */
static int pa_start(void)
{
	int err;

	pa.retry = 0;
	err = mqueue_alloc(&pa.q, qh, NULL);
	if (err)
		return err;

	tmr_init(&pa.rc);
	return paconn_start(&pa.paconn);
}


/**
 * Getter for pulseaudio connection struct
 *
 * @return struct paconn_st* pulseaudio connection object
 */
struct paconn_st *paconn_get(void)
{
	return pa.paconn ? pa.paconn : NULL;
}


static void dev_info_notify_cb(pa_operation *op, void *arg)
{
	(void) arg;

	if (pa_operation_get_state(op) != PA_OPERATION_DONE)
		return;

	pa_operation_cancel(op);
	pa_operation_unref(op);
}


int pulse_set_available_devices(struct list *dev_list,
	pa_operation *(get_dev_info_cb)(pa_context *, struct list*))
{
	pa_operation *op = NULL;

	if (pa_context_get_state(pa.paconn->context) != PA_CONTEXT_READY)
		return EINVAL;

	op = get_dev_info_cb(pa.paconn->context, dev_list);
	if (!op)
		return EINVAL;

	pa_operation_set_state_callback(op, dev_info_notify_cb, NULL);
	return 0;
}


static int module_init(void)
{
	int err;

	memset(&pa, 0, sizeof(pa));
	err = pa_start();
	if (err)
		return err;

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "pulse", pulse_player_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "pulse", pulse_recorder_alloc);

	return err;
}


static int module_close(void)
{
	mem_deref(pa.paconn);
	mem_deref(pa.q);
	tmr_cancel(&pa.rc);

	auplay = mem_deref(auplay);
	ausrc  = mem_deref(ausrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(pulse) = {
	"pulse",
	"audio",
	module_init,
	module_close,
};
