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

	device_stop(st->dev);

	mem_deref(st->dev);
}


int play_alloc(struct auplay_st **stp, const struct auplay *ap,
	       struct auplay_prm *prm, const char *device,
	       auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err;

	if (!stp || !ap || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("aubridge: playback: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap  = ap;
	st->prm = *prm;
	st->wh  = wh;
	st->arg = arg;

	err = device_connect(&st->dev, device, st, NULL);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
