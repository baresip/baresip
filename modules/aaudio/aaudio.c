/**
 * @file aaudio/aaudio.c AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aaudio.h"


static struct auplay *auplay;
static struct ausrc *ausrc;


static int module_init(void)
{
	int err;

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "aaudio", aaudio_player_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "aaudio", aaudio_recorder_alloc);

	return err;
}


static int module_close(void)
{

	auplay = mem_deref(auplay);
	ausrc = mem_deref(ausrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aaudio) = {
	"aaudio",
	"audio",
	module_init,
	module_close,
};
