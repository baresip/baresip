/**
 * @file menc.c  Media encryption
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Register a new Media encryption module
 *
 * @param mencl List of Media-encryption modules
 * @param menc  Media encryption module
 */
void menc_register(struct list *mencl, struct menc *menc)
{
	if (!mencl || !menc)
		return;

	list_append(mencl, &menc->le, menc);

	info("mediaenc: %s\n", menc->id);
}


/**
 * Unregister a Media encryption module
 *
 * @param menc Media encryption module
 */
void menc_unregister(struct menc *menc)
{
	if (!menc)
		return;

	list_unlink(&menc->le);
}


/**
 * Find a Media Encryption module by name
 *
 * @param mencl List of Media-encryption modules
 * @param id    Name of the Media Encryption module to find
 *
 * @return Matching Media Encryption module if found, otherwise NULL
 */
const struct menc *menc_find(const struct list *mencl, const char *id)
{
	struct le *le;

	if (!mencl)
		return NULL;

	for (le = mencl->head; le; le = le->next) {
		struct menc *me = le->data;

		if (0 == str_casecmp(id, me->id))
			return me;
	}

	return NULL;
}


/**
 * Get the name of a media encryption event
 *
 * @param event Media encryption event
 *
 * @return String with media encryption event name
 */
const char *menc_event_name(enum menc_event event)
{
	switch (event) {

	case MENC_EVENT_SECURE:         return "Secure";
	case MENC_EVENT_VERIFY_REQUEST: return "Verify Request";
	case MENC_EVENT_PEER_VERIFIED:  return "Peer Verified";
	default: return "?";
	}
}
