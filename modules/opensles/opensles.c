/**
 * @file opensles.c  OpenSLES audio driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include <SLES/OpenSLES.h>
#include "SLES/OpenSLES_Android.h"
#include "opensles.h"


SLObjectItf engineObject = NULL;
SLEngineItf engineEngine;


static struct auplay *auplay;
static struct ausrc *ausrc;


static int module_init(void)
{
	SLresult r;
	int err;

	r = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	r = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE,
					  &engineEngine);
	if (SL_RESULT_SUCCESS != r)
		return ENODEV;

	err  = auplay_register(&auplay, "opensles", opensles_player_alloc);
	err |= ausrc_register(&ausrc, "opensles", opensles_recorder_alloc);

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
