/**
 * @file vidsrc.c Video Source
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


/** Video Source state */
struct vidsrc_st {
	struct vidsrc *vs;  /**< Video Source */
};


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


/**
 * Allocate a new video source state
 *
 * @param stp     Pointer to allocated state
 * @param vidsrcl List of Video Sources
 * @param name    Name of the video source
 * @param ctx     Optional media context
 * @param prm     Video source parameters
 * @param size    Wanted video size of the source
 * @param fmt     Format parameter
 * @param dev     Video device
 * @param frameh  Video frame handler
 * @param errorh  Error handler (optional)
 * @param arg     Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int vidsrc_alloc(struct vidsrc_st **stp, struct list *vidsrcl,
		 const char *name,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt, const char *dev,
		 vidsrc_frame_h *frameh, vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc *vs = (struct vidsrc *)vidsrc_find(vidsrcl, name);
	if (!vs)
		return ENOENT;

	return vs->alloch(stp, vs, ctx, prm, size, fmt, dev,
			  frameh, errorh, arg);
}


/**
 * Get the video source module from a video source state
 *
 * @param st Video source state
 *
 * @return Video source module
 */
struct vidsrc *vidsrc_get(struct vidsrc_st *st)
{
	return st ? st->vs : NULL;
}
