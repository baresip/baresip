/**
 * @file wasapi/src.c Windows Audio Session API (WASAPI)
 *
 * Copyright (C) 2024 Sebastian Reimers
 * Copyright (C) 2024 AGFEO GmbH & Co. KG
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "wasapi.h"


struct ausrc_st {
	struct ausrc_prm prm;
	ausrc_read_h *rh;
	void *arg;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

}


int wasapi_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		      struct ausrc_prm *prm, const char *device,
		      ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;

	(void)device;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->rh  = rh;
	st->arg = arg;
	st->prm = *prm;

	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
