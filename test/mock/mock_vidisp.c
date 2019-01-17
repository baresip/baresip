/**
 * @file mock/mock_vidisp.c Mock video display
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "../test.h"


#define MAX_WIDTH 65536
#define MAX_HEIGHT 65536


struct vidisp_st {
	const struct vidisp *vd;  /* inheritance */
	unsigned n_frame;
};


static struct {
	mock_vidisp_h *disph;
	void *arg;
} mock;


static void disp_destructor(void *arg)
{
	struct vidisp_st *st = arg;
	(void)st;
}


static int mock_disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
		      struct vidisp_prm *prm, const char *dev,
		      vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	(void)prm;
	(void)dev;
	(void)resizeh;
	(void)arg;

	if (!stp || !vd)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), disp_destructor);
	if (!st)
		return ENOMEM;

	st->vd = vd;

	*stp = st;

	return 0;
}


static int mock_display(struct vidisp_st *st, const char *title,
			const struct vidframe *frame, uint64_t timestamp)
{
	unsigned width, height;
	(void)title;
	(void)timestamp;

	if (!st || !frame)
		return EINVAL;

	width = frame->size.w;
	height = frame->size.h;

	if (!vidframe_isvalid(frame)) {
		warning("mock_vidisp: got invalid frame\n");
		return EPROTO;
	}

	/* verify that the video frame is good */
	if (frame->fmt >= VID_FMT_N)
		return EPROTO;
	if (width == 0 || width > MAX_WIDTH)
		return EPROTO;
	if (height == 0 || height > MAX_HEIGHT)
		return EPROTO;
	if (frame->linesize[0] == 0)
		return EPROTO;

	++st->n_frame;

	if (st->n_frame >= 10) {
		info("mock_vidisp: got %u frames\n", st->n_frame);

		if (mock.disph)
			mock.disph(frame, timestamp, mock.arg);
	}

	return 0;
}


int mock_vidisp_register(struct vidisp **vidispp,
			 mock_vidisp_h *disph, void *arg)
{
	mock.disph = disph;
	mock.arg = arg;

	return vidisp_register(vidispp, baresip_vidispl(), "mock-vidisp",
			       mock_disp_alloc, NULL, mock_display, NULL);
}
