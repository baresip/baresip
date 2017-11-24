/**
 * @file pulse.c  Pulseaudio sound driver
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

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
