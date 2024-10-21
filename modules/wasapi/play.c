/**
 * @file wasapi/play.c Windows Audio Session API (WASAPI)
 *
 * Copyright (C) 2024 Sebastian Reimers
 * Copyright (C) 2024 AGFEO GmbH & Co. KG
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "wasapi.h"


struct auplay_st {
	struct auplay_prm prm;
	auplay_write_h *wh;
	void *arg;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;
}


int wasapi_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err;
	(void)device;

	if (!stp || !ap || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->wh	= wh;
	st->arg = arg;
	st->prm = *prm;

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
