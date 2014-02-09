/**
 * @file v4l.c Video4Linux video-source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _BSD_SOURCE 1
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#undef __STRICT_ANSI__ /* needed for RHEL4 kernel 2.6.9 */
#include <libv4l1-videodev.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


struct vidsrc_st {
	struct vidsrc *vs;  /* inheritance */

	int fd;
	pthread_t thread;
	bool run;
	struct vidsz size;
	struct mbuf *mb;
	vidsrc_frame_h *frameh;
	void *arg;
};


static struct vidsrc *vidsrc;


static void v4l_get_caps(struct vidsrc_st *st)
{
	struct video_capability caps;

	if (-1 == ioctl(st->fd, VIDIOCGCAP, &caps)) {
		warning("v4l: VIDIOCGCAP: %m\n", errno);
		return;
	}

	info("v4l: video: \"%s\" (%ux%u) - (%ux%u)\n", caps.name,
	     caps.minwidth, caps.minheight,
	     caps.maxwidth, caps.maxheight);

	if (VID_TYPE_CAPTURE != caps.type) {
		warning("v4l: not a capture device (%d)\n", caps.type);
	}
}


static int v4l_check_palette(struct vidsrc_st *st)
{
	struct video_picture pic;

	if (-1 == ioctl(st->fd, VIDIOCGPICT, &pic)) {
		warning("v4l: VIDIOCGPICT: %m\n", errno);
		return errno;
	}

	if (VIDEO_PALETTE_RGB24 != pic.palette) {
		warning("v4l: unsupported palette %d (only RGB24 supp.)\n",
			pic.palette);
		return ENODEV;
	}

	return 0;
}


static int v4l_get_win(int fd, int width, int height)
{
	struct video_window win;

	if (-1 == ioctl(fd, VIDIOCGWIN, &win)) {
		warning("v4l: VIDIOCGWIN: %m\n", errno);
		return errno;
	}

	info("v4l: video window: x,y=%u,%u (%u x %u)\n",
	     win.x, win.y, win.width, win.height);

	win.width = width;
	win.height = height;

	if (-1 == ioctl(fd, VIDIOCSWIN, &win)) {
		warning("v4l: VIDIOCSWIN: %m\n", errno);
		return errno;
	}

	return 0;
}


static void call_frame_handler(struct vidsrc_st *st, uint8_t *buf)
{
	struct vidframe frame;

	vidframe_init_buf(&frame, VID_FMT_RGB32, &st->size, buf);

	st->frameh(&frame, st->arg);
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;

	while (st->run) {
		ssize_t n;

		n = read(st->fd, st->mb->buf, st->mb->size);
		if ((ssize_t)st->mb->size != n) {
			warning("v4l: video read: %d -> %d bytes\n",
				st->mb->size, n);
			continue;
		}

		call_frame_handler(st, st->mb->buf);
	}

	return NULL;
}


static int vd_open(struct vidsrc_st *v4l, const char *device)
{
	/* NOTE: with kernel 2.6.26 it takes ~2 seconds to open
	 *       the video device.
	 */
	v4l->fd = open(device, O_RDWR);
	if (v4l->fd < 0) {
		warning("v4l: open %s: %m\n", device, errno);
		return errno;
	}

	return 0;
}


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->fd >= 0)
		close(st->fd);

	mem_deref(st->mb);
	mem_deref(st->vs);
}


static uint32_t rgb24_size(const struct vidsz *sz)
{
	return sz ? (sz->w * sz->h * 24/8) : 0;
}


static int alloc(struct vidsrc_st **stp, struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err;

	(void)ctx;
	(void)prm;
	(void)fmt;
	(void)errorh;

	if (!stp || !size || !frameh)
		return EINVAL;

	if (!str_isset(dev))
		dev = "/dev/video0";

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = mem_ref(vs);
	st->fd     = -1;
	st->size   = *size;
	st->frameh = frameh;
	st->arg    = arg;

	info("v4l: open: %s (%u x %u)\n", dev, size->w, size->h);

	err = vd_open(st, dev);
	if (err)
		goto out;

	v4l_get_caps(st);

	err = v4l_check_palette(st);
	if (err)
		goto out;

	err = v4l_get_win(st->fd, st->size.w, st->size.h);
	if (err)
		goto out;

	/* note: assumes RGB24 */
	st->mb = mbuf_alloc(rgb24_size(&st->size));
	if (!st->mb) {
		err = ENOMEM;
		goto out;
	}

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


static int v4l_init(void)
{
	return vidsrc_register(&vidsrc, "v4l", alloc, NULL);
}


static int v4l_close(void)
{
	vidsrc = mem_deref(vidsrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(v4l) = {
	"v4l",
	"vidsrc",
	v4l_init,
	v4l_close
};
