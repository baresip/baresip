/**
 * @file mnat.c Media NAT
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static void destructor(void *arg)
{
	struct mnat *mnat = arg;

	list_unlink(&mnat->le);
}


/**
 * Register a Media NAT traversal module
 *
 * @param mnatp    Pointer to allocated Media NAT traversal module
 * @param mnatl    List of Media-NAT modules
 * @param id       Media NAT Identifier
 * @param ftag     SIP Feature tag (optional)
 * @param sessh    Session allocation handler
 * @param mediah   Media allocation handler
 * @param updateh  Update handler
 *
 * @return 0 if success, otherwise errorcode
 */
int mnat_register(struct mnat **mnatp, struct list *mnatl,
		  const char *id, const char *ftag,
		  mnat_sess_h *sessh, mnat_media_h *mediah,
		  mnat_update_h *updateh)
{
	struct mnat *mnat;

	if (!mnatp || !id || !sessh || !mediah)
		return EINVAL;

	mnat = mem_zalloc(sizeof(*mnat), destructor);
	if (!mnat)
		return ENOMEM;

	list_append(mnatl, &mnat->le, mnat);

	mnat->id      = id;
	mnat->ftag    = ftag;
	mnat->sessh   = sessh;
	mnat->mediah  = mediah;
	mnat->updateh = updateh;

	info("medianat: %s\n", id);

	*mnatp = mnat;

	return 0;
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
