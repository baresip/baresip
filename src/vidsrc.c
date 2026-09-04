/**
 * @file vidsrc.c Video Source
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static void destructor(void *arg)
{
	struct vidsrc *vs = arg;

	list_flush(&vs->dev_list);
	list_unlink(&vs->le);
}


/**
 * Register a Video Source
 *
 * @param vsp     Pointer to allocated Video Source
 * @param vidsrcl List of Video Sources
 * @param name    Name of Video Source
 * @param alloch  Allocation handler
 * @param updateh Update handler
 *
 * @return 0 if success, otherwise errorcode
 */
int vidsrc_register(struct vidsrc **vsp, struct list *vidsrcl,
		    const char *name,
		    vidsrc_alloc_h *alloch, vidsrc_update_h *updateh)
{
	struct vidsrc *vs;

	if (!vsp || !vidsrcl)
		return EINVAL;

	vs = mem_zalloc(sizeof(*vs), destructor);
	if (!vs)
		return ENOMEM;

	list_append(vidsrcl, &vs->le, vs);

	vs->name    = name;
	vs->alloch  = alloch;
	vs->updateh = updateh;

	info("vidsrc: %s\n", name);

	*vsp = vs;

	return 0;
}


/**
 * Find a Video Source by name
 *
 * @param vidsrcl List of Video Sources
 * @param name    Name of the Video Source to find
 *
 * @return Matching Video Source if found, otherwise NULL
 */
const struct vidsrc *vidsrc_find(const struct list *vidsrcl, const char *name)
{
	struct le *le;

	for (le=list_head(vidsrcl); le; le=le->next) {

		struct vidsrc *vs = le->data;

		if (str_isset(name) && 0 != str_casecmp(name, vs->name))
			continue;

		return vs;
	}

	return NULL;
}
