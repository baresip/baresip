/**
 * @file aubridge/play.c Audio bridge -- playback
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aubridge.h"


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	aubridge_device_stop(st->dev);

	mem_deref(st->dev);
}


int aubridge_play_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err;

	if (!stp || !ap || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->prm = *prm;
	st->wh  = wh;
	st->arg = arg;

	err = aubridge_device_connect(&st->dev, device, st, NULL);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
