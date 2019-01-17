/**
 * @file v4l.c Video4Linux video-source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
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


/**
 * @defgroup v4l v4l
 *
 * Video4Linux video-source module
 */


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */

	int fd;
	pthread_t thread;
	bool run;
	struct vidsz size;
	struct mbuf *mb;
	enum vidfmt fmt;
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

	switch (pic.palette) {

	case VIDEO_PALETTE_RGB24:
		st->fmt = VID_FMT_RGB32;
		break;

	case VIDEO_PALETTE_YUYV:
		st->fmt = VID_FMT_YUYV422;
		break;

	default:
		warning("v4l: unsupported palette %d\n", pic.palette);
		return ENODEV;
	}

	info("v4l: pixel format is %s\n", vidfmt_name(st->fmt));

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


static void call_frame_handler(struct vidsrc_st *st, uint8_t *buf,
			       uint64_t timestamp)
{
	struct vidframe frame;

	vidframe_init_buf(&frame, st->fmt, &st->size, buf);

	st->frameh(&frame, timestamp, st->arg);
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;

	while (st->run) {
		ssize_t n;
		uint64_t timestamp;

		n = read(st->fd, st->mb->buf, st->mb->size);
		if ((ssize_t)st->mb->size != n) {
			warning("v4l: video read: %d -> %d bytes\n",
				st->mb->size, n);
			continue;
		}

		timestamp = tmr_jiffies_usec();

		call_frame_handler(st, st->mb->buf, timestamp);
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

	st->vs     = vs;
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

	/* allocate buffer for the picture */
	st->mb = mbuf_alloc(vidframe_size(st->fmt, &st->size));
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
	return vidsrc_register(&vidsrc, baresip_vidsrcl(), "v4l", alloc, NULL);
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
