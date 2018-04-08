/**
 * @file x11grab.c X11 grabbing video-source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <unistd.h>
#ifndef SOLARIS
#define _XOPEN_SOURCE 1
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup x11grab x11grab
 *
 * X11 window-grabbing video-source module
 *
 *
 * XXX: add option to select a specific X window and x,y offset
 */


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */
	Display *disp;
	XImage *image;
	pthread_t thread;
	bool run;
	int fps;
	struct vidsz size;
	enum vidfmt pixfmt;
	vidsrc_frame_h *frameh;
	void *arg;
};


static struct vidsrc *vidsrc;


static int x11grab_open(struct vidsrc_st *st, const struct vidsz *sz)
{
	int x = 0, y = 0;

	st->disp = XOpenDisplay(NULL);
	if (!st->disp) {
		warning("x11grab: error opening display\n");
		return ENODEV;
	}

	st->image = XGetImage(st->disp,
			      RootWindow(st->disp, DefaultScreen(st->disp)),
			      x, y, sz->w, sz->h, AllPlanes, ZPixmap);
	if (!st->image) {
		warning("x11grab: error creating Ximage\n");
		return ENODEV;
	}

	switch (st->image->bits_per_pixel) {

	case 32:
		st->pixfmt = VID_FMT_RGB32;
		break;

	case 16:
		st->pixfmt = (st->image->green_mask == 0x7e0)
			? VID_FMT_RGB565
			: VID_FMT_RGB555;
		break;

	default:
		warning("x11grab: not supported: bpp=%d\n",
			st->image->bits_per_pixel);
		return ENOSYS;
	}

	return 0;
}


static inline uint8_t *x11grab_read(struct vidsrc_st *st)
{
	const int x = 0, y = 0;
	XImage *im;

	im = XGetSubImage(st->disp,
			  RootWindow(st->disp, DefaultScreen(st->disp)),
			  x, y, st->size.w, st->size.h, AllPlanes, ZPixmap,
			  st->image, 0, 0);
	if (!im)
		return NULL;

	return (uint8_t *)st->image->data;
}


static void call_frame_handler(struct vidsrc_st *st, uint8_t *buf,
			       uint64_t timestamp)
{
	struct vidframe frame;

	vidframe_init_buf(&frame, st->pixfmt, &st->size, buf);

	st->frameh(&frame, timestamp, st->arg);
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;
	uint64_t ts = tmr_jiffies();
	uint8_t *buf;

	while (st->run) {

		uint64_t timestamp;

		if (tmr_jiffies() < ts) {
			sys_msleep(4);
			continue;
		}

		buf = x11grab_read(st);
		if (!buf)
			continue;

		timestamp = ts * VIDEO_TIMEBASE / 1000;

		ts += (1000/st->fps);

		call_frame_handler(st, buf, timestamp);
	}

	return NULL;
}


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->image)
		XDestroyImage(st->image);

	if (st->disp)
		XCloseDisplay(st->disp);
}


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err;

	(void)ctx;
	(void)fmt;
	(void)dev;
	(void)errorh;

	if (!stp || !prm || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->size   = *size;
	st->fps    = prm->fps;
	st->frameh = frameh;
	st->arg    = arg;

	err = x11grab_open(st, size);
	if (err)
		goto out;

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int x11grab_init(void)
{
	return vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "x11grab", alloc, NULL);
}


static int x11grab_close(void)
{
	vidsrc = mem_deref(vidsrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(x11grab) = {
	"x11grab",
	"vidsrc",
	x11grab_init,
	x11grab_close
};
