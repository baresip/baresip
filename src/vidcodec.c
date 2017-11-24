/**
 * @file vidcodec.c Video Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>


/**
 * Register a Video Codec
 *
 * @param vidcodecl List of video-codecs
 * @param vc        Video Codec
 */
void vidcodec_register(struct list *vidcodecl, struct vidcodec *vc)
{
	if (!vidcodecl || !vc)
		return;

	list_append(vidcodecl, &vc->le, vc);

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
 * @param vidcodecl List of video-codecs
 * @param name      Name of the Video Codec to find
 * @param variant   Codec Variant
 *
 * @return Matching Video Codec if found, otherwise NULL
 */
const struct vidcodec *vidcodec_find(const struct list *vidcodecl,
				     const char *name, const char *variant)
{
	struct le *le;

	for (le=list_head(vidcodecl); le; le=le->next) {

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
 * Find a Video Encoder by name
 *
 * @param vidcodecl List of video-codecs
 * @param name      Name of the Video Encoder to find
 *
 * @return Matching Video Encoder if found, otherwise NULL
 */
const struct vidcodec *vidcodec_find_encoder(const struct list *vidcodecl,
					     const char *name)
{
	struct le *le;

	for (le=list_head(vidcodecl); le; le=le->next) {

		struct vidcodec *vc = le->data;

		if (name && 0 != str_casecmp(name, vc->name))
			continue;

		if (vc->ench)
			return vc;
	}

	return NULL;
}


/**
 * Find a Video Decoder by name
 *
 * @param vidcodecl List of video-codecs
 * @param name      Name of the Video Decoder to find
 *
 * @return Matching Video Decoder if found, otherwise NULL
 */
const struct vidcodec *vidcodec_find_decoder(const struct list *vidcodecl,
					     const char *name)
{
	struct le *le;

	for (le=list_head(vidcodecl); le; le=le->next) {

		struct vidcodec *vc = le->data;

		if (name && 0 != str_casecmp(name, vc->name))
			continue;

		if (vc->dech)
			return vc;
	}

	return NULL;
}
