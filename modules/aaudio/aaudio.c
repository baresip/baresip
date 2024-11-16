/**
 * @file aaudio.c  AAudio audio driver
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include <aaudio/AAudio.h>

#include "aaudio.h"


/**
 * @defgroup aaudio aaudio
 *
 * Aaudio audio driver module for Android
 */


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

	if (playerStream)
		AAudioStream_close(playerStream);
	if (recorderStream)
		AAudioStream_close(recorderStream);

	auplay = (struct auplay *)mem_deref((void *)auplay);
	ausrc = (struct ausrc *)mem_deref((void *)ausrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aaudio) = {
	"aaudio",
	"audio",
	module_init,
	module_close,
};
