/**
 * @file aucodec.c Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Register an Audio Codec
 *
 * @param aucodecl List of audio-codecs
 * @param ac       Audio Codec object
 */
void aucodec_register(struct list *aucodecl, struct aucodec *ac)
{
	if (!aucodecl || !ac)
		return;

	list_append(aucodecl, &ac->le, ac);

	info("aucodec: %s/%u/%u\n", ac->name, ac->srate, ac->ch);
}


/**
 * Unregister an Audio Codec
 *
 * @param ac Audio Codec object
 */
void aucodec_unregister(struct aucodec *ac)
{
	if (!ac)
		return;

	list_unlink(&ac->le);
}


/**
 * Find an Audio Codec
 *
 * @param aucodecl List of audio-codecs
 * @param name     Audio codec name
 * @param srate    Audio codec sampling rate
 * @param ch       Audio codec number of channels
 *
 * @return Matching audio codec if found, NULL if not found
 */
const struct aucodec *aucodec_find(const struct list *aucodecl,
				   const char *name, uint32_t srate,
				   uint8_t ch)
{
	struct le *le;

	for (le=list_head(aucodecl); le; le=le->next) {

		struct aucodec *ac = le->data;

		if (name && 0 != str_casecmp(name, ac->name))
			continue;

		if (srate && srate != ac->srate)
			continue;

		if (ch && ch != ac->ch)
			continue;

		return ac;
	}

	return NULL;
}
