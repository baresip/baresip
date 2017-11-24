/**
 * @file winwave.c Windows sound driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <windows.h>
#include <mmsystem.h>
#include <baresip.h>
#include "winwave.h"


/**
 * @defgroup winwave winwave
 *
 * Windows audio driver module
 *
 */


static struct ausrc *ausrc;
static struct auplay *auplay;


static int ww_init(void)
{
	int play_dev_count, src_dev_count;
	int err;

	play_dev_count = waveOutGetNumDevs();
	src_dev_count = waveInGetNumDevs();

	info("winwave: output devices: %d, input devices: %d\n",
	     play_dev_count, src_dev_count);

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "winwave", winwave_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "winwave", winwave_play_alloc);

	return err;
}


static int ww_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(winwave) = {
	"winwave",
	"sound",
	ww_init,
	ww_close
};
