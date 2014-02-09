/**
 * @file v4l2.c  Video4Linux2 video-source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _BSD_SOURCE 1
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#undef __STRICT_ANSI__ /* needed for RHEL4 kernel 2.6.9 */
#include <linux/videodev2.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libv4l2.h>


enum io_method {
	IO_METHOD_READ = 0,
	IO_METHOD_MMAP
};

struct buffer {
	void  *start;
	size_t length;
};

struct vidsrc_st {
	struct vidsrc *vs;  /* inheritance */

	int fd;
	pthread_t thread;
	bool run;
	struct vidsz sz, app_sz;
	struct mbuf *mb;
	vidsrc_frame_h *frameh;
	void *arg;
	enum io_method io;
	struct buffer *buffers;
	unsigned int   n_buffers;
};


static struct vidsrc *vidsrc;


static void get_video_input(struct vidsrc_st *st)
{
	struct v4l2_input input;

	memset(&input, 0, sizeof(input));

	if (-1 == v4l2_ioctl(st->fd, VIDIOC_G_INPUT, &input.index)) {
		warning("v4l2: VIDIOC_G_INPUT: %m\n", errno);
		return;
	}

	if (-1 == v4l2_ioctl(st->fd, VIDIOC_ENUMINPUT, &input)) {
		warning("v4l2: VIDIOC_ENUMINPUT: %m\n", errno);
		return;
	}

	info("v4l2: Current input: %s\n", input.name);
}


static int xioctl(int fd, unsigned long int request, void *arg)
{
	int r;

	do {
		r = v4l2_ioctl(fd, request, arg);
	}
	while (-1 == r && EINTR == errno);

	return r;
}


static int init_read(struct vidsrc_st *st, unsigned int buffer_size)
{
	st->buffers = calloc(1, sizeof (*st->buffers));
	if (!st->buffers)
		return ENOMEM;

	st->buffers[0].length = buffer_size;
	st->buffers[0].start = malloc(buffer_size);
	if (!st->buffers[0].start)
		return ENOMEM;

	return 0;
}


static int init_mmap(struct vidsrc_st *st, const char *dev_name)
{
	struct v4l2_requestbuffers req;

	memset(&req, 0, sizeof(req));

	req.count  = 4;
	req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(st->fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			warning("v4l2: %s does not support "
				"memory mapping\n", dev_name);
			return errno;
		}
		else {
			return errno;
		}
	}

	if (req.count < 2) {
		warning("v4l2: Insufficient buffer memory on %s\n", dev_name);
		return ENOMEM;
	}

	st->buffers = calloc(req.count, sizeof(*st->buffers));
	if (!st->buffers)
		return ENOMEM;

	for (st->n_buffers = 0; st->n_buffers<req.count; ++st->n_buffers) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));

		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = st->n_buffers;

		if (-1 == xioctl(st->fd, VIDIOC_QUERYBUF, &buf)) {
			warning("v4l2: VIDIOC_QUERYBUF\n");
			return errno;
		}

		st->buffers[st->n_buffers].length = buf.length;
		st->buffers[st->n_buffers].start =
			v4l2_mmap(NULL /* start anywhere */,
				  buf.length,
				  PROT_READ | PROT_WRITE /* required */,
				  MAP_SHARED /* recommended */,
				  st->fd, buf.m.offset);

		if (MAP_FAILED == st->buffers[st->n_buffers].start) {
			warning("v4l2: mmap failed\n");
			return ENODEV;
		}
	}

	return 0;
}


static int v4l2_init_device(struct vidsrc_st *st, const char *dev_name)
{
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	unsigned int min;
	const char *pix;
	int err;

	if (-1 == xioctl(st->fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			warning("v4l2: %s is no V4L2 device\n", dev_name);
			return ENODEV;
		}
		else {
			warning("v4l2: VIDIOC_QUERYCAP: %m\n", errno);
			return errno;
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		warning("v4l2: %s is no video capture device\n", dev_name);
		return ENODEV;
	}

	switch (st->io) {

	case IO_METHOD_READ:
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
			warning("%s does not support read i/o\n", dev_name);
			return ENOSYS;
		}
		break;

	case IO_METHOD_MMAP:
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			warning("v4l2: %s does not support streaming i/o\n",
				dev_name);
			return ENOSYS;
		}
		break;
	}

	/* Select video input, video standard and tune here. */

	memset(&fmt, 0, sizeof(fmt));

	fmt.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = st->app_sz.w;
	fmt.fmt.pix.height      = st->app_sz.h;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;

	if (-1 == xioctl(st->fd, VIDIOC_S_FMT, &fmt)) {
		warning("v4l2: VIDIOC_S_FMT: %m\n", errno);
		return errno;
	}

	/* Note VIDIOC_S_FMT may change width and height. */

	/* Buggy driver paranoia. */
	min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min)
		fmt.fmt.pix.bytesperline = min;
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min)
		fmt.fmt.pix.sizeimage = min;

	st->sz.w = fmt.fmt.pix.width;
	st->sz.h = fmt.fmt.pix.height;

	if (!vidsz_cmp(&st->sz, &st->app_sz)) {
		info("v4l2: scaling %u x %u  --->  %u x %u\n",
		     st->sz.w, st->sz.h, st->app_sz.w, st->app_sz.h);
	}

	switch (st->io) {

	case IO_METHOD_READ:
		err = init_read(st, fmt.fmt.pix.sizeimage);
		break;

	case IO_METHOD_MMAP:
		err = init_mmap(st, dev_name);
		break;

	default:
		warning("v4l2: unknown io: %d\n", st->io);
		err = EINVAL;
		break;
	}

	if (err)
		return err;

	pix = (char *)&fmt.fmt.pix.pixelformat;

	if (V4L2_PIX_FMT_YUV420 != fmt.fmt.pix.pixelformat) {
		warning("v4l2: %s: expected YUV420 got %c%c%c%c\n", dev_name,
			pix[0], pix[1], pix[2], pix[3]);
		return ENODEV;
	}

	info("v4l2: %s: found valid V4L2 device (%u x %u) pixfmt=%c%c%c%c\n",
	       dev_name, fmt.fmt.pix.width, fmt.fmt.pix.height,
	       pix[0], pix[1], pix[2], pix[3]);

	return 0;
}


static void stop_capturing(struct vidsrc_st *st)
{
	enum v4l2_buf_type type;

	if (st->fd < 0)
		return;

	switch (st->io) {

	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (-1 == xioctl(st->fd, VIDIOC_STREAMOFF, &type))
			warning("v4l2: VIDIOC_STREAMOFF\n");
		break;
	}
}


static void uninit_device(struct vidsrc_st *st)
{
	unsigned int i;

	switch (st->io) {

	case IO_METHOD_READ:
		if (st->buffers)
			free(st->buffers[0].start);
		break;

	case IO_METHOD_MMAP:
		for (i=0; i<st->n_buffers; ++i)
			v4l2_munmap(st->buffers[i].start,
				    st->buffers[i].length);
		break;
	}

	free(st->buffers);
}


static int start_capturing(struct vidsrc_st *st, int fd)
{
	unsigned int i;
	enum v4l2_buf_type type;

	switch (st->io) {

	case IO_METHOD_READ:
		/* Nothing to do. */
		break;

	case IO_METHOD_MMAP:
		for (i = 0; i < st->n_buffers; ++i) {
	    		struct v4l2_buffer buf;

			memset(&buf, 0, sizeof(buf));

			buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index  = i;

			if (-1 == xioctl (fd, VIDIOC_QBUF, &buf))
		    		return errno;
		}

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (-1 == xioctl (fd, VIDIOC_STREAMON, &type))
			return errno;

		break;
	}

	return 0;
}


static void call_frame_handler(struct vidsrc_st *st, uint8_t *buf)
{
	struct vidframe frame;

	vidframe_init_buf(&frame, VID_FMT_YUV420P, &st->sz, buf);

	st->frameh(&frame, st->arg);
}


static int read_frame(struct vidsrc_st *st)
{
	struct v4l2_buffer buf;
	ssize_t n;

	switch (st->io) {

	case IO_METHOD_READ:
		n = v4l2_read(st->fd, st->mb->buf, st->mb->size);
		if (-1 == n) {
	    		switch (errno) {

	    		case EAGAIN:
		    		return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				warning("v4l2: read error: %m\n", errno);
				BREAKPOINT;
				return errno;
			}
		}

		call_frame_handler(st, st->mb->buf);
		break;

	case IO_METHOD_MMAP:
		memset(&buf, 0, sizeof(buf));

	    	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    	buf.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl (st->fd, VIDIOC_DQBUF, &buf)) {
	    		switch (errno) {

			case EAGAIN:
		    		return 0;

			case EIO:
				/* Could ignore EIO, see spec. */

				/* fall through */

			default:
				warning("v4l2: VIDIOC_DQBUF: %m\n", errno);
				return errno;
			}
		}

		if (buf.index >= st->n_buffers) {
			warning("v4l2: index >= n_buffers\n");
		}

		call_frame_handler(st, st->buffers[buf.index].start);

		if (-1 == xioctl (st->fd, VIDIOC_QBUF, &buf)) {
			warning("v4l2: VIDIOC_QBUF\n");
			return errno;
		}
		break;
	}

	return 0;
}


static int vd_open(struct vidsrc_st *st, const char *device)
{
	/* NOTE: with kernel 2.6.26 it takes ~2 seconds to open
	 *       the video device.
	 */
	st->fd = v4l2_open(device, O_RDWR);
	if (st->fd < 0) {
		warning("v4l2: open %s: %m\n", device, errno);
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

	stop_capturing(st);
	uninit_device(st);

	if (st->fd >= 0)
		v4l2_close(st->fd);

	mem_deref(st->mb);
	mem_deref(st->vs);
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;
	int err;

	while (st->run) {
		err = read_frame(st);
		if (err) {
			warning("v4l2: read_frame: %m\n", err);
		}
	}

	return NULL;
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

	st->vs = mem_ref(vs);
	st->fd = -1;
	st->io = IO_METHOD_MMAP;

	st->app_sz = *size;
	st->frameh = frameh;
	st->arg    = arg;

	err = vd_open(st, dev);
	if (err)
		goto out;

	/* Try Video4Linux 2 first .. */
	err = v4l2_init_device(st, dev);
	if (err)
		goto out;

	get_video_input(st);

	st->mb = mbuf_alloc(st->app_sz.w * st->app_sz.h * 3 / 2);
	if (!st->mb) {
		err = ENOMEM;
		goto out;
	}

	err = start_capturing(st, st->fd);
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


static int v4l_init(void)
{
	return vidsrc_register(&vidsrc, "v4l2", alloc, NULL);
}


static int v4l_close(void)
{
	vidsrc = mem_deref(vidsrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(v4l2) = {
	"v4l2",
	"vidsrc",
	v4l_init,
	v4l_close
};
