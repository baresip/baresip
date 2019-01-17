/**
 * @file pulse.c  Pulseaudio sound driver
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "pulse.h"


/**
 * @defgroup pulse pulse
 *
 * Audio driver module for Pulseaudio
 *
 * This module is experimental and work-in-progress. It is using
 * the pulseaudio "simple" interface.
 */


static struct auplay *auplay;
static struct ausrc *ausrc;


static int module_init(void)
{
	int err;

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "pulse", pulse_player_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "pulse", pulse_recorder_alloc);

	if (err)
		return err;

	err  = pulse_player_init(auplay);
	err |= pulse_recorder_init(ausrc);

	return err;
}


int set_available_devices(struct list *dev_list,
			  pa_operation *(get_dev_info_cb)(pa_context *,
							  struct list *))
{
	pa_mainloop *pa_ml = NULL;
	pa_mainloop_api *pa_mlapi = NULL;
	pa_operation *pa_op = NULL;
	pa_context *pa_ctx = NULL;
	int err = 0, pa_error = 0;

	/* Create a mainloop API and connection to the default server */
	pa_ml = pa_mainloop_new();
	if (!pa_ml){
		warning("pulse: mainloop_new failed\n");
		err = 1;
		goto out;
	}

	pa_mlapi = pa_mainloop_get_api(pa_ml);
	if (!pa_ml){
		warning("pulse: pa_mainloop_get_api failed\n");
		err = 1;
		goto out;
	}

	pa_ctx = pa_context_new(pa_mlapi, "Baresip");
	if (pa_context_connect(pa_ctx, NULL, 0, NULL) < 0) {
		warning("pulse: pa_context_connect failed: (%s)\n",
				 pa_strerror(pa_context_errno(pa_ctx)));
		err = 1;
		goto out;
	}

	while (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY) {
		pa_error = pa_mainloop_iterate(pa_ml, 1, NULL);
		if (pa_error < 0) {
			warning("pulse: pa_mainloop_iterate failed\n");
			err = 1;
			goto out;
		}
	}

	pa_op = get_dev_info_cb(pa_ctx, dev_list);

	while (pa_operation_get_state(pa_op) != PA_OPERATION_DONE) {
		pa_error = pa_mainloop_iterate(pa_ml, 1, NULL);
		if (pa_error < 0) {
			warning("pulse: pa_mainloop_iterate failed\n");
			err = 1;
			goto out;
		}
	}


out:
	if (pa_op)
		pa_operation_unref(pa_op);
	if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
		pa_context_unref(pa_ctx);
	}
	if (pa_ml)
		pa_mainloop_free(pa_ml);

	return err;
}


static int module_close(void)
{
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
