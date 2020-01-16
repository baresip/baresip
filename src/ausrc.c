/**
 * @file ausrc.c Audio Source
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static void destructor(void *arg)
{
	struct ausrc *as = arg;

	list_flush(&as->dev_list);
	list_unlink(&as->le);
}


/**
 * Register an Audio Source
 *
 * @param asp     Pointer to allocated Audio Source object
 * @param ausrcl  List of Audio Sources
 * @param name    Audio Source name
 * @param alloch  Allocation handler
 *
 * @return 0 if success, otherwise errorcode
 */
int ausrc_register(struct ausrc **asp, struct list *ausrcl,
		   const char *name, ausrc_alloc_h *alloch)
{
	struct ausrc *as;

	if (!asp)
		return EINVAL;

	as = mem_zalloc(sizeof(*as), destructor);
	if (!as)
		return ENOMEM;

	list_append(ausrcl, &as->le, as);

	as->name   = name;
	as->alloch = alloch;

	info("ausrc: %s\n", name);

	*asp = as;

	return 0;
}


/**
 * Find an Audio Source by name
 *
 * @param ausrcl List of Audio Sources
 * @param name   Name of the Audio Source to find
 *
 * @return Matching Audio Source if found, otherwise NULL
 */
const struct ausrc *ausrc_find(const struct list *ausrcl, const char *name)
{
	struct le *le;

	for (le=list_head(ausrcl); le; le=le->next) {

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
 * @param ausrcl List of Audio Sources
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
int ausrc_alloc(struct ausrc_st **stp, struct list *ausrcl,
		struct media_ctx **ctx,
		const char *name, struct ausrc_prm *prm, const char *device,
		ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc *as;

	as = (struct ausrc *)ausrc_find(ausrcl, name);
	if (!as)
		return ENOENT;

	return as->alloch(stp, as, ctx, prm, device, rh, errh, arg);
}


/**
 * Get the audio source module from a audio source state
 *
 * @param st Audio source state
 *
 * @return Audio source module
 */
struct ausrc *ausrc_get(struct ausrc_st *st)
{
	return st ? (struct ausrc *)st->as : NULL;
}
