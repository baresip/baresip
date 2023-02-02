/**
 * @file pipewire.c  Pipewire sound driver
 *
 * Copyright (C) 2023 Commend.com - c.spielberger@commend.com
 */

#include <string.h>
#include <errno.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <spa/param/audio/raw.h>
#include <pipewire/pipewire.h>

#include "pipewire.h"

/**
 * @defgroup pipewire pipewire
 *
 * Audio driver module for Pipewire
 *
 */

enum {
	RECONN_DELAY = 1500,
};


struct pw_stat {
	struct pw_thread_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
};


static struct pw_stat *d = NULL;

static struct auplay *auplay = NULL;
static struct ausrc *ausrc = NULL;


static void destructor(void *arg)
{
	struct pw_stat *pw = arg;

	if (pw->core)
		pw_core_disconnect(pw->core);

	if (pw->context)
		pw_context_destroy(pw->context);

	if (pw->loop) {
		pw_thread_loop_stop(pw->loop);
		pw_thread_loop_destroy(pw->loop);
	}
}


static struct pw_stat *pw_stat_alloc(void)
{
	struct pw_stat *pw;
	int err;

	pw = mem_zalloc(sizeof(*pw), destructor);

	pw->loop = pw_thread_loop_new("baresip pipewire", NULL);
	if (!pw->loop)
		goto errout;

	err = pw_thread_loop_start(pw->loop);
	if (err)
		goto errout;

	pw->context = pw_context_new(pw_thread_loop_get_loop(pw->loop),
				     NULL /* properties */,
				     0 /* user_data size */);
	if (!pw->context)
		goto errout;

	pw->core = pw_context_connect(pw->context,
				      NULL /* properties */,
				      0 /* user_data size */);
	if (!pw->core)
		goto errout;

	info("pipewire: connected to pipewire\n");
	return pw;

errout:
	warning("pipewire: could not connect to pipewire\n");
	mem_deref(pw);
	return NULL;
}


struct pw_core *pw_core_instance(void)
{
	if (!d)
		return NULL;

	return d->core;
}


struct pw_thread_loop *pw_loop_instance(void)
{
	if (!d)
		return NULL;

	return d->loop;
}


int aufmt_to_pw_format(enum aufmt fmt)
{
	switch (fmt) {
		case AUFMT_S16LE:  return SPA_AUDIO_FORMAT_S16_LE;
		case AUFMT_FLOAT:  return SPA_AUDIO_FORMAT_F32;
		default: return 0;
	}
}


static int module_init(void)
{
	int err = 0;

	pw_init(NULL, NULL);
	info("pipewire: headers %s library %s \n",
	     pw_get_headers_version(), pw_get_library_version());

	d = pw_stat_alloc();
	if (!d)
		return errno;

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "pipewire", pw_playback_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "pipewire", pw_capture_alloc);

	return err;
}


static int module_close(void)
{
	auplay = mem_deref(auplay);
	ausrc  = mem_deref(ausrc);

	d = mem_deref(d);
	pw_deinit();
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(pipewire) = {
	"pipewire",
	"audio",
	module_init,
	module_close,
};
