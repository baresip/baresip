/**
 * @file opensles.c  OpenSLES audio driver
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include <SLES/OpenSLES.h>
#include "SLES/OpenSLES_Android.h"
#include "opensles.h"


/**
 * @defgroup opensles opensles
 *
 * Audio driver module for Android OpenSLES
 */


SLObjectItf engineObject = NULL;
SLEngineItf engineEngine;


static struct auplay *auplay;
static struct ausrc *ausrc;


static int module_init(void)
{
	SLEngineOption engineOption[] = {
		{ (SLuint32) SL_ENGINEOPTION_THREADSAFE,
		  (SLuint32) SL_BOOLEAN_TRUE },
	};
	SLresult r;
	int err;

	r = slCreateEngine(&engineObject, 1, engineOption, 0, NULL, NULL);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
					  &engineEngine);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	err  = auplay_register(&auplay, baresip_auplayl(),
			       "opensles", opensles_player_alloc);
	err |= ausrc_register(&ausrc, baresip_ausrcl(),
			      "opensles", opensles_recorder_alloc);

	return err;
}


static int module_close(void)
{
	auplay = mem_deref(auplay);
	ausrc = mem_deref(ausrc);

	if (engineObject != NULL) {
		(*engineObject)->Destroy(engineObject);
		engineObject = NULL;
		engineEngine = NULL;
	}

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(opensles) = {
	"opensles",
	"audio",
	module_init,
	module_close,
};
