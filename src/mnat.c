/**
 * @file mnat.c Media NAT
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Register a Media NAT traversal module
 *
 * @param mnatl    List of Media-NAT modules
 * @param mnat     Media NAT traversal module
 */
void mnat_register(struct list *mnatl, struct mnat *mnat)
{
	if (!mnatl || !mnat)
		return;

	list_append(mnatl, &mnat->le, mnat);

	info("medianat: %s\n", mnat->id);
}


/**
 * Unregister a Media NAT traversal module
 *
 * @param mnat     Media NAT traversal module
 */
void mnat_unregister(struct mnat *mnat)
{
	if (!mnat)
		return;

	list_unlink(&mnat->le);
}


/**
 * Find a Media NAT module by name
 *
 * @param mnatl List of Media-NAT modules
 * @param id    Name of the Media NAT module to find
 *
 * @return Matching Media NAT module if found, otherwise NULL
 */
const struct mnat *mnat_find(const struct list *mnatl, const char *id)
{
	struct mnat *mnat;
	struct le *le;

	if (!mnatl)
		return NULL;

	for (le=mnatl->head; le; le=le->next) {

		mnat = le->data;

		if (str_casecmp(mnat->id, id))
			continue;

		return mnat;
	}

	return NULL;
}
