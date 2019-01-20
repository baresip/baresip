/**
 * @file aubridge.c Audio bridge
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "aubridge.h"


/**
 * @defgroup aubridge aubridge
 *
 * Audio bridge module
 *
 * This module can be used to connect two audio devices together,
 * so that all output to AUPLAY device is bridged as the input to
 * a AUSRC device.
 *
 * Sample config:
 *
 \verbatim
  audio_player            aubridge,pseudo0
  audio_source            aubridge,pseudo0
 \endverbatim
 */


static struct ausrc *ausrc;
static struct auplay *auplay;

struct hash *aubridge_ht_device;


static int module_init(void)
{
	int err;

	err = hash_alloc(&aubridge_ht_device, 32);
	if (err)
		return err;

	err  = ausrc_register(&ausrc, baresip_ausrcl(), "aubridge",
			      aubridge_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "aubridge", aubridge_play_alloc);

	return err;
}


static int module_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	aubridge_ht_device = mem_deref(aubridge_ht_device);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aubridge) = {
	"aubridge",
	"audio",
	module_init,
	module_close,
};
