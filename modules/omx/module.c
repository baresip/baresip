/**
 * @file omx/module.c     Raspberry Pi VideoCoreIV OpenMAX interface
 *
 * Copyright (C) 2016 - 2017 Creytiv.com
 * Copyright (C) 2016 - 2017 Jonathan Sieber
 */


#include "omx.h"

#include <stdlib.h>

#include <re.h>
#include <rem.h>
#include <baresip.h>


struct vidisp_st {
	const struct vidisp *vd;  /* inheritance */
	struct vidsz size;
	struct omx_state* omx;
};

static struct vidisp* vid;

static struct omx_state omx;


static void destructor(void *arg)
{
	struct vidisp_st *st = arg;
	omx_display_disable(st->omx);
}


static int omx_vidisp_alloc(struct vidisp_st **vp, const struct vidisp *vd,
		     struct vidisp_prm *prm, const char *dev,
		     vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;

	/* Not used by OMX */
	(void) prm;
	(void) dev;
	(void) resizeh;
	(void) arg;

	info("omx: vidisp_alloc\n");

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vd = vd;
	*vp = st;

	st->omx = &omx;

	return 0;
}


static int omx_vidisp_display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame, uint64_t timestamp)
{
	int err = 0;
	void* buf;
	uint32_t len;
	struct vidframe omx_frame;
	(void)title;
	(void)timestamp;

	if (frame->fmt != VID_FMT_YUV420P) {
		return EINVAL;
	}

	if (!vidsz_cmp(&st->size, &frame->size)) {
		info("omx: new frame size: w=%d h=%d\n",
			frame->size.w, frame->size.h);
		info("omx: linesize[0]=%d\tlinesize[1]=%d\tlinesize[2]=%d\n",
			frame->linesize[0], frame->linesize[1],
			frame->linesize[2]);
		err = omx_display_enable(st->omx,
			frame->size.w, frame->size.h, frame->size.w);
		if (err) {
			warning("omx_display_enable failed");
			return err;
		}
		st->size = frame->size;
	}

	/* Get Buffer Pointer */
	omx_display_input_buffer(st->omx, &buf, &len);

	vidframe_init_buf(&omx_frame, VID_FMT_YUV420P, &frame->size,
			  buf);

	vidconv(&omx_frame, frame, 0);

	omx_display_flush_buffer(st->omx);
	return 0;
}


static int module_init(void)
{
	if (omx_init(&omx) != 0) {
		warning("Could not initialize OpenMAX");
		return ENODEV;
	}

	return vidisp_register(&vid, baresip_vidispl(), "omx",
		omx_vidisp_alloc, NULL, omx_vidisp_display, NULL);
}


static int module_close(void)
{
	/* HACK: not deinitializing OMX because of a hangup */
	/* omx_deinit(&omx) */
	vid = mem_deref(vid);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(omx) = {
	"omx",
	"vidisp",
	module_init,
	module_close
};
