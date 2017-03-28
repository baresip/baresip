/**
 * @file omx/module.c     Raspberry Pi VideoCoreIV OpenMAX interface
 *
 * Copyright (C) 2016 - 2017 Creytiv.com
 * Copyright (C) 2016 - 2017 Jonathan Sieber
 */


#include "omx.h"

#include <stdlib.h>

#include <re/re.h>
#include <rem/rem.h>
#include <baresip.h>

int omx_vidisp_alloc(struct vidisp_st **vp, const struct vidisp* vd,
	struct vidisp_prm *prm, const char *dev, vidisp_resize_h *resizeh,
	void *arg);
int omx_vidisp_display(struct vidisp_st *st, const char *title,
	const struct vidframe *frame);

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

int omx_vidisp_alloc(struct vidisp_st **vp, const struct vidisp* vd,
	struct vidisp_prm *prm,	const char *dev, vidisp_resize_h *resizeh,
	void *arg)
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


int omx_vidisp_display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame)
{
	int i;
	void* buf;
	uint32_t len;
	uint32_t offset = 0;

	size_t plane_bytes;
	void* dest;

	int err = 0;

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
			frame->size.w, frame->size.h, frame->linesize[0]);
		if (err) {
			error("omx_display_enable failed");
			return err;
		}
		st->size = frame->size;
		memcpy(&st->size, &frame->size, sizeof(struct vidsz));
	}

	/* Get Buffer Pointer */
	omx_display_input_buffer(st->omx, &buf, &len);

	for (i = 0; i < 3; i++) {
		plane_bytes = frame->linesize[i] *
			frame->size.h / (i > 0 ? 2 : 1);
		dest = buf + offset;

		if (offset + plane_bytes > len) {
			warn("Too large frame for OMX Buffer size. Expected"
				" %d, got %d\n", len, offset + plane_bytes);
			return ENOMEM;
		}

		memcpy(dest, frame->data[i], plane_bytes);
		offset += plane_bytes;
	}
	omx_display_flush_buffer(st->omx);
	return 0;
}

static int module_init(void)
{
	if (!omx_init(&omx)) {
		error("Could not initialize OpenMAX");
		return ENODEV;
	}

	return vidisp_register(&vid, "omx",
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
