/**
 * @file vidbridge/src.c Video bridge -- source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vidbridge.h"


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->vidisp)
		st->vidisp->vidsrc = NULL;

	list_unlink(&st->le);
	mem_deref(st->device);
}


int vidbridge_src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
			struct media_ctx **ctx, struct vidsrc_prm *prm,
			const struct vidsz *size, const char *fmt,
			const char *dev, vidsrc_frame_h *frameh,
			vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err;
	(void)ctx;
	(void)fmt;
	(void)errorh;

	if (!stp || !prm || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->frameh = frameh;
	st->arg    = arg;
	st->fps    = prm->fps;

	err = str_dup(&st->device, dev);
	if (err)
		goto out;

	/* find a vidisp device with same name */
	st->vidisp = vidbridge_disp_find(dev);
	if (st->vidisp) {
		st->vidisp->vidsrc = st;
	}

	hash_append(ht_src, hash_joaat_str(dev), &st->le, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static bool list_apply_handler(struct le *le, void *arg)
{
	struct vidsrc_st *st = le->data;

	return 0 == str_cmp(st->device, arg);
}


struct vidsrc_st *vidbridge_src_find(const char *device)
{
	return list_ledata(hash_lookup(ht_src, hash_joaat_str(device),
				       list_apply_handler, (void *)device));
}


void vidbridge_src_input(struct vidsrc_st *st,
			 const struct vidframe *frame, uint64_t timestamp)
{
	if (!st || !frame)
		return;

	if (st->frameh)
		st->frameh((struct vidframe *)frame, timestamp, st->arg);
}
