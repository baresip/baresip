/**
 * @file aucodec.c Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static struct list aucodecl;


/**
 * Register an Audio Codec
 *
 * @param ac Audio Codec object
 */
void aucodec_register(struct aucodec *ac)
{
	if (!ac)
		return;

	list_append(&aucodecl, &ac->le, ac);

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


const struct aucodec *aucodec_find(const char *name, uint32_t srate,
				   uint8_t ch)
{
	struct le *le;

	for (le=aucodecl.head; le; le=le->next) {

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


/**
 * Get the list of Audio Codecs
 */
struct list *aucodec_list(void)
{
	return &aucodecl;
}
