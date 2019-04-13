/**
 * @file vidfilt.c Video Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Register a new Video Filter
 *
 * @param vidfiltl List of Video-Filters
 * @param vf       Video Filter to register
 */
void vidfilt_register(struct list *vidfiltl, struct vidfilt *vf)
{
	if (!vf)
		return;

	list_append(vidfiltl, &vf->le, vf);

	info("vidfilt: %s\n", vf->name);
}


/**
 * Unregister a Video Filter
 *
 * @param vf Video Filter to unregister
 */
void vidfilt_unregister(struct vidfilt *vf)
{
	if (!vf)
		return;

	list_unlink(&vf->le);
}


static void vidfilt_enc_destructor(void *arg)
{
	struct vidfilt_enc_st *st = arg;

	list_unlink(&st->le);
}


/**
 * Allocate a video-filter encode state and append to list
 *
 * @param filtl List of video-filter states
 * @param ctx   Media context
 * @param vf    Video filter
 * @param prm   Video filter parameters
 * @param vid   Pointer to video object (optional)
 *
 * @return 0 if success, otherwise errorcode
 */
int vidfilt_enc_append(struct list *filtl, void **ctx,
		       const struct vidfilt *vf, struct vidfilt_prm *prm,
		       const struct video *vid)
{
	struct vidfilt_enc_st *st = NULL;
	int err;

	if (vf->encupdh) {
		err = vf->encupdh(&st, ctx, vf, prm, vid);
		if (err)
			return err;
	}
	else {
		st = mem_zalloc(sizeof(*st), vidfilt_enc_destructor);
		if (!st)
			return ENOMEM;
	}

	st->vf = vf;
	list_append(filtl, &st->le, st);

	return 0;
}


static void vidfilt_dec_destructor(void *arg)
{
	struct vidfilt_dec_st *st = arg;

	list_unlink(&st->le);
}


/**
 * Allocate a video-filter decode state and append to list
 *
 * @param filtl List of video-filter states
 * @param ctx   Media context
 * @param vf    Video filter
 * @param prm   Video filter parameters
 * @param vid   Pointer to video object (optional)
 *
 * @return 0 if success, otherwise errorcode
 */
int vidfilt_dec_append(struct list *filtl, void **ctx,
		       const struct vidfilt *vf, struct vidfilt_prm *prm,
		       const struct video *vid)
{
	struct vidfilt_dec_st *st = NULL;
	int err;

	if (vf->decupdh) {
		err = vf->decupdh(&st, ctx, vf, prm, vid);
		if (err)
			return err;
	}
	else {
		st = mem_zalloc(sizeof(*st), vidfilt_dec_destructor);
		if (!st)
			return ENOMEM;
	}

	st->vf = vf;
	list_append(filtl, &st->le, st);

	return 0;
}
