/**
 * @file vidbridge/disp.c Video bridge -- display
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vidbridge.h"


static void destructor(void *arg)
{
	struct vidisp_st *st = arg;

	if (st->vidsrc)
		st->vidsrc->vidisp = NULL;

	list_unlink(&st->le);
	mem_deref(st->device);
}


int vidbridge_disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
			 struct vidisp_prm *prm, const char *dev,
			 vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err = 0;
	(void)prm;
	(void)resizeh;
	(void)arg;

	if (!stp || !vd || !dev)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vd = vd;

	err = str_dup(&st->device, dev);
	if (err)
		goto out;

	/* find the vidsrc with the same device-name */
	st->vidsrc = vidbridge_src_find(dev);
	if (st->vidsrc) {
		st->vidsrc->vidisp = st;
	}

	hash_append(ht_disp, hash_joaat_str(dev), &st->le, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static bool list_apply_handler(struct le *le, void *arg)
{
	struct vidisp_st *st = le->data;

	return 0 == str_cmp(st->device, arg);
}


int vidbridge_disp_display(struct vidisp_st *st, const char *title,
			   const struct vidframe *frame, uint64_t timestamp)
{
	int err = 0;
	(void)title;

	if (st->vidsrc)
		vidbridge_src_input(st->vidsrc, frame, timestamp);
	else {
		debug("vidbridge: display: dropping frame (%u x %u)\n",
		      frame->size.w, frame->size.h);
	}

	return err;
}


struct vidisp_st *vidbridge_disp_find(const char *device)
{
	return list_ledata(hash_lookup(ht_disp, hash_joaat_str(device),
				       list_apply_handler, (void *)device));
}
