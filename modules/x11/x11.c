/**
 * @file x11.c Video driver for X11
 *
 * Copyright (C) 2010 Creytiv.com
 */

#ifndef SOLARIS
#define _XOPEN_SOURCE 1
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


struct vidisp_st {
	struct vidisp *vd;              /**< Inheritance (1st)     */
	struct vidsz size;              /**< Current size          */

	Display *disp;
	Window win;
	GC gc;
	XImage *image;
	XShmSegmentInfo shm;
	bool xshmat;
	bool internal;
	enum vidfmt pixfmt;
};


static struct vidisp *vid;       /**< X11 Video-display      */

static struct {
	int shm_error;
	int (*errorh) (Display *, XErrorEvent *);
} x11;


/* NOTE: Global handler */
static int error_handler(Display *d, XErrorEvent *e)
{
	if (e->error_code == BadAccess)
		x11.shm_error = 1;
	else if (x11.errorh)
		return x11.errorh(d, e);

	return 0;
}


static void destructor(void *arg)
{
	struct vidisp_st *st = arg;

	if (st->image) {
		st->image->data = NULL;
		XDestroyImage(st->image);
	}

	if (st->gc)
		XFreeGC(st->disp, st->gc);

	if (st->xshmat)
		XShmDetach(st->disp, &st->shm);

	if (st->shm.shmaddr != (char *)-1)
		shmdt(st->shm.shmaddr);

	if (st->shm.shmid >= 0)
		shmctl(st->shm.shmid, IPC_RMID, NULL);

	if (st->disp) {
		if (st->internal && st->win)
			XDestroyWindow(st->disp, st->win);

		XCloseDisplay(st->disp);
	}

	mem_deref(st->vd);
}


static int create_window(struct vidisp_st *st, const struct vidsz *sz)
{
	st->win = XCreateSimpleWindow(st->disp, DefaultRootWindow(st->disp),
				      0, 0, sz->w, sz->h, 1, 0, 0);
	if (!st->win) {
		warning("x11: failed to create X window\n");
		return ENOMEM;
	}

	XClearWindow(st->disp, st->win);
	XMapRaised(st->disp, st->win);

	return 0;
}


static int x11_reset(struct vidisp_st *st, const struct vidsz *sz)
{
	XWindowAttributes attrs;
	XGCValues gcv;
	size_t bufsz, pixsz;
	int err = 0;

	if (!XGetWindowAttributes(st->disp, st->win, &attrs)) {
		warning("x11: cant't get window attributes\n");
		return EINVAL;
	}

	switch (attrs.depth) {

	case 24:
		st->pixfmt = VID_FMT_RGB32;
		pixsz = 4;
		break;

	case 16:
		st->pixfmt = VID_FMT_RGB565;
		pixsz = 2;
		break;

	case 15:
		st->pixfmt = VID_FMT_RGB555;
		pixsz = 2;
		break;

	default:
		warning("x11: colordepth not supported: %d\n", attrs.depth);
		return ENOSYS;
	}

	bufsz = sz->w * sz->h * pixsz;

	if (st->image) {
		XDestroyImage(st->image);
		st->image = NULL;
	}

	if (st->xshmat)
		XShmDetach(st->disp, &st->shm);

	if (st->shm.shmaddr != (char *)-1)
		shmdt(st->shm.shmaddr);

	if (st->shm.shmid >= 0)
		shmctl(st->shm.shmid, IPC_RMID, NULL);

	st->shm.shmid = shmget(IPC_PRIVATE, bufsz, IPC_CREAT | 0777);
	if (st->shm.shmid < 0) {
		warning("x11: failed to allocate shared memory\n");
		return ENOMEM;
	}

	st->shm.shmaddr = shmat(st->shm.shmid, NULL, 0);
	if (st->shm.shmaddr == (char *)-1) {
		warning("x11: failed to attach to shared memory\n");
		return ENOMEM;
	}

	st->shm.readOnly = true;

	x11.shm_error = 0;
	x11.errorh = XSetErrorHandler(error_handler);

	if (!XShmAttach(st->disp, &st->shm)) {
		warning("x11: failed to attach X to shared memory\n");
		return ENOMEM;
	}

	XSync(st->disp, False);
	XSetErrorHandler(x11.errorh);

	if (x11.shm_error)
		info("x11: shared memory disabled\n");
	else
		st->xshmat = true;

	gcv.graphics_exposures = false;

	st->gc = XCreateGC(st->disp, st->win, GCGraphicsExposures, &gcv);
	if (!st->gc) {
		warning("x11: failed to create graphics context\n");
		return ENOMEM;
	}

	if (st->xshmat) {
		st->image = XShmCreateImage(st->disp, attrs.visual,
					    attrs.depth, ZPixmap,
					    st->shm.shmaddr, &st->shm,
					    sz->w, sz->h);
	}
	else {
		st->image = XCreateImage(st->disp, attrs.visual,
					 attrs.depth, ZPixmap, 0,
					 st->shm.shmaddr,
					 sz->w, sz->h, 32, 0);

	}
	if (!st->image) {
		warning("x11: Failed to create X image\n");
		return ENOMEM;
	}

	XResizeWindow(st->disp, st->win, sz->w, sz->h);

	st->size = *sz;

	return err;
}


/* prm->view points to the XWINDOW ID */
static int alloc(struct vidisp_st **stp, struct vidisp *vd,
		 struct vidisp_prm *prm, const char *dev,
		 vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err = 0;
	(void)dev;
	(void)resizeh;
	(void)arg;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vd = mem_ref(vd);
	st->shm.shmaddr = (char *)-1;

	st->disp = XOpenDisplay(NULL);
	if (!st->disp) {
		warning("x11: could not open X display\n");
		err = ENODEV;
		goto out;
	}

	/* Use provided view, or create our own */
	if (prm && prm->view)
		st->win = (Window)prm->view;
	else
		st->internal = true;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int display(struct vidisp_st *st, const char *title,
		   const struct vidframe *frame)
{
	struct vidframe frame_rgb;
	int err = 0;

	if (!vidsz_cmp(&st->size, &frame->size)) {
		char capt[256];

		if (st->size.w && st->size.h) {
			info("x11: reset: %u x %u  --->  %u x %u\n",
			     st->size.w, st->size.h,
			     frame->size.w, frame->size.h);
		}

		if (st->internal && !st->win)
			err = create_window(st, &frame->size);

		err |= x11_reset(st, &frame->size);
		if (err)
			return err;

		if (title) {
			re_snprintf(capt, sizeof(capt), "%s - %u x %u",
				    title, frame->size.w, frame->size.h);
		}
		else {
			re_snprintf(capt, sizeof(capt), "%u x %u",
				    frame->size.w, frame->size.h);
		}

		XStoreName(st->disp, st->win, capt);
	}

	/* Convert from YUV420P to RGB */

	vidframe_init_buf(&frame_rgb, st->pixfmt, &frame->size,
			  (uint8_t *)st->shm.shmaddr);

	vidconv(&frame_rgb, frame, 0);

	/* draw */
	if (st->xshmat)
		XShmPutImage(st->disp, st->win, st->gc, st->image,
			     0, 0, 0, 0, st->size.w, st->size.h, false);
	else
		XPutImage(st->disp, st->win, st->gc, st->image,
			  0, 0, 0, 0, st->size.w, st->size.h);

	XSync(st->disp, false);

	return err;
}


static void hide(struct vidisp_st *st)
{
	if (!st)
		return;

	if (st->win)
		XLowerWindow(st->disp, st->win);
}


static int module_init(void)
{
	return vidisp_register(&vid, "x11", alloc, NULL, display, hide);
}


static int module_close(void)
{
	vid = mem_deref(vid);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(x11) = {
	"x11",
	"vidisp",
	module_init,
	module_close,
};
