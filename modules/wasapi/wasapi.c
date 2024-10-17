/**
 * @file wasapi/wasapi.c Windows Audio Session API (WASAPI)
 *
 * Copyright (C) 2024 Sebastian Reimers
 * Copyright (C) 2024 AGFEO GmbH & Co. KG
 */

#include <re.h>
#include <baresip.h>
#include "wasapi.h"


static struct auplay *auplay;
static struct ausrc *ausrc;


static int wasapi_init(void)
{
	int err;

	err = ausrc_register(&ausrc, baresip_ausrcl(), "wasapi",
			     wasapi_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(), "wasapi",
			       wasapi_play_alloc);
	if (err)
		return err;

	return err;
}


static int wasapi_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(wasapi) = {
	"wasapi",
	"sound",
	wasapi_init,
	wasapi_close
};
