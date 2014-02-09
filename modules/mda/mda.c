/**
 * @file mda.c  Symbian MDA audio driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "mda.h"


static struct auplay *auplay;
static struct ausrc *ausrc;


static int module_init(void)
{
	int err;

	err  = auplay_register(&auplay, "mda", mda_player_alloc);
	err |= ausrc_register(&ausrc, "mda", mda_recorder_alloc);

	return err;
}


static int module_close(void)
{
	auplay = mem_deref(auplay);
	ausrc  = mem_deref(ausrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(mda) = {
	"mda",
	"audio",
	module_init,
	module_close,
};
