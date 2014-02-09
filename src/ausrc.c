/**
 * @file ausrc.c Audio Source
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static struct list ausrcl = LIST_INIT;


static void destructor(void *arg)
{
	struct ausrc *as = arg;

	list_unlink(&as->le);
}


/**
 * Register an Audio Source
 *
 * @param asp     Pointer to allocated Audio Source object
 * @param name    Audio Source name
 * @param alloch  Allocation handler
 *
 * @return 0 if success, otherwise errorcode
 */
int ausrc_register(struct ausrc **asp, const char *name, ausrc_alloc_h *alloch)
{
	struct ausrc *as;

	if (!asp)
		return EINVAL;

	as = mem_zalloc(sizeof(*as), destructor);
	if (!as)
		return ENOMEM;

	list_append(&ausrcl, &as->le, as);

	as->name   = name;
	as->alloch = alloch;

	info("ausrc: %s\n", name);

	*asp = as;

	return 0;
}


/**
 * Find an Audio Source by name
 *
 * @param name Name of the Audio Source to find
 *
 * @return Matching Audio Source if found, otherwise NULL
 */
const struct ausrc *ausrc_find(const char *name)
{
	struct le *le;

	for (le=ausrcl.head; le; le=le->next) {

		struct ausrc *as = le->data;

		if (str_isset(name) && 0 != str_casecmp(name, as->name))
			continue;

		return as;
	}

	return NULL;
}


/**
 * Allocate an Audio Source state
 *
 * @param stp    Pointer to allocated Audio Source state
 * @param ctx    Media context (optional)
 * @param name   Name of Audio Source
 * @param prm    Audio Source parameters
 * @param device Name of Audio Source device (driver specific)
 * @param rh     Read handler
 * @param errh   Error handler
 * @param arg    Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int ausrc_alloc(struct ausrc_st **stp, struct media_ctx **ctx,
		const char *name, struct ausrc_prm *prm, const char *device,
		ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc *as;

	as = (struct ausrc *)ausrc_find(name);
	if (!as)
		return ENOENT;

	return as->alloch(stp, as, ctx, prm, device, rh, errh, arg);
}
