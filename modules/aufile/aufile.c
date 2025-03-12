/**
 * @file aufile.c WAV Audio Source
 *
 * Copyright (C) 2015 Alfred E. Heggestad
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aufile.h"


/**
 * @defgroup aufile aufile
 *
 * Audio module for using a WAV-file as audio input
 *
 * Sample config:
 *
 \verbatim
  audio_source            aufile,/tmp/test.wav
 \endverbatim
 */


static struct ausrc *ausrc;
static struct auplay *auplay;

static int module_init(void)
{
	int err;
	err = ausrc_register(&ausrc, baresip_ausrcl(), "aufile",
			     aufile_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(), "aufile",
			       aufile_play_alloc);
	if (err)
		return err;

	ausrc->infoh = aufile_info_handler;
	return 0;
}


static int module_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aufile) = {
	"aufile",
	"ausrc",
	module_init,
	module_close
};
