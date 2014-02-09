/**
 * @file vidcodec.c Video Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>


static struct list vidcodecl;


/**
 * Register a Video Codec
 *
 * @param vc Video Codec
 */
void vidcodec_register(struct vidcodec *vc)
{
	if (!vc)
		return;

	list_append(&vidcodecl, &vc->le, vc);

	info("vidcodec: %s\n", vc->name);
}


/**
 * Unregister a Video Codec
 *
 * @param vc Video Codec
 */
void vidcodec_unregister(struct vidcodec *vc)
{
	if (!vc)
		return;

	list_unlink(&vc->le);
}


/**
 * Find a Video Codec by name
 *
 * @param name    Name of the Video Codec to find
 * @param variant Codec Variant
 *
 * @return Matching Video Codec if found, otherwise NULL
 */
const struct vidcodec *vidcodec_find(const char *name, const char *variant)
{
	struct le *le;

	for (le=vidcodecl.head; le; le=le->next) {

		struct vidcodec *vc = le->data;

		if (name && 0 != str_casecmp(name, vc->name))
			continue;

		if (variant && 0 != str_casecmp(variant, vc->variant))
			continue;

		return vc;
	}

	return NULL;
}


/**
 * Get the list of Video Codecs
 *
 * @return List of Video Codecs
 */
struct list *vidcodec_list(void)
{
	return &vidcodecl;
}
