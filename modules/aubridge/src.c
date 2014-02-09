/**
 * @file aubridge/src.c Audio bridge -- source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "aubridge.h"


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	device_stop(st->dev);

	mem_deref(st->dev);
	mem_deref(st->as);
}


int src_alloc(struct ausrc_st **stp, struct ausrc *as,
	      struct media_ctx **ctx,
	      struct ausrc_prm *prm, const char *device,
	      ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err = 0;
	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->as   = mem_ref(as);
	st->prm  = *prm;
	st->rh   = rh;
	st->arg  = arg;

	err = device_connect(&st->dev, device, NULL, st);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
