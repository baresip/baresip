/**
 * @file v4l2.c  Video4Linux2 video-source
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
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
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#if defined (OPENBSD) || defined (NETBSD)
#include <sys/videoio.h>
#else
#include <linux/videodev2.h>
#endif

#ifdef HAVE_LIBV4L2
#include <libv4l2.h>
#else
#define v4l2_open open
#define v4l2_read read
#define v4l2_ioctl ioctl
#define v4l2_mmap mmap
#define v4l2_munmap munmap
#define v4l2_close close
#endif


/**
 * @defgroup v4l2 v4l2
 *
 * V4L2 (Video for Linux 2) video-source module
 */


struct buffer {
	void  *start;
	size_t length;
};

struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */

	int fd;
	pthread_t thread;
	bool run;
	struct vidsz sz;
	u_int32_t pixfmt;
	struct buffer *buffers;
	unsigned int   n_buffers;
	vidsrc_frame_h *frameh;
	void *arg;
};


static struct vidsrc *vidsrc;


static enum vidfmt match_fmt(u_int32_t fmt)
{
	switch (fmt) {

	case V4L2_PIX_FMT_YUV420: return VID_FMT_YUV420P;
	case V4L2_PIX_FMT_YUYV:   return VID_FMT_YUYV422;
	case V4L2_PIX_FMT_UYVY:   return VID_FMT_UYVY422;
	case V4L2_PIX_FMT_RGB32:  return VID_FMT_RGB32;
	case V4L2_PIX_FMT_RGB565: return VID_FMT_RGB565;
	case V4L2_PIX_FMT_RGB555: return VID_FMT_RGB555;
	case V4L2_PIX_FMT_NV12:   return VID_FMT_NV12;
	case V4L2_PIX_FMT_NV21:   return VID_FMT_NV21;
	default:                  return VID_FMT_N;
	}
}


static void print_video_input(const struct vidsrc_st *st)
{
	struct v4l2_input input;

	memset(&input, 0, sizeof(input));

#ifndef OPENBSD
	if (-1 == v4l2_ioctl(st->fd, VIDIOC_G_INPUT, &input.index)) {
		warning("v4l2: VIDIOC_G_INPUT: %m\n", errno);
		return;
	}
#endif

	if (-1 == v4l2_ioctl(st->fd, VIDIOC_ENUMINPUT, &input)) {
		warning("v4l2: VIDIOC_ENUMINPUT: %m\n", errno);
		return;
	}

	info("v4l2: Current input: \"%s\"\n", input.name);
}


static void print_framerate(const struct vidsrc_st *st)
{
	struct v4l2_streamparm streamparm;
	struct v4l2_fract tpf;
	double fps;

	memset(&streamparm, 0, sizeof(streamparm));

	streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (v4l2_ioctl(st->fd, VIDIOC_G_PARM, &streamparm) != 0) {
		warning("v4l2: VIDIOC_G_PARM error (%m)\n", errno);
		return;
	}

	tpf = streamparm.parm.capture.timeperframe;
	fps = (double)tpf.denominator / (double)tpf.numerator;

	info("v4l2: current framerate is %.2f fps\n", fps);
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

	st->buffers = mem_zalloc(req.count * sizeof(*st->buffers), NULL);
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


static int v4l2_init_device(struct vidsrc_st *st, const char *dev_name,
			    int width, int height)
{
	struct v4l2_capability cap;
	struct v4l2_format fmt;
	struct v4l2_fmtdesc fmts;
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

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		warning("v4l2: %s does not support streaming i/o\n",
			dev_name);
		return ENOSYS;
	}

	/* Negotiate video format */
	memset(&fmts, 0, sizeof(fmts));

	fmts.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	for (fmts.index=0; !v4l2_ioctl(st->fd, VIDIOC_ENUM_FMT, &fmts);
			fmts.index++) {
		if (match_fmt(fmts.pixelformat) != VID_FMT_N) {
			st->pixfmt = fmts.pixelformat;
			break;
		}
	}

	if (!st->pixfmt) {
		warning("v4l2: format negotiation failed: %m\n", errno);
		return errno;
	}

	/* Select video input, video standard and tune here. */

	memset(&fmt, 0, sizeof(fmt));

	fmt.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = width;
	fmt.fmt.pix.height      = height;
	fmt.fmt.pix.pixelformat = st->pixfmt;
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

	err = init_mmap(st, dev_name);
	if (err)
		return err;

	pix = (char *)&fmt.fmt.pix.pixelformat;

	if (st->pixfmt != fmt.fmt.pix.pixelformat) {
		warning("v4l2: %s: unexpectedly got %c%c%c%c\n", dev_name,
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

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	xioctl(st->fd, VIDIOC_STREAMOFF, &type);
}


static void uninit_device(struct vidsrc_st *st)
{
	unsigned int i;

	for (i=0; i<st->n_buffers; ++i) {
		v4l2_munmap(st->buffers[i].start, st->buffers[i].length);
	}

	st->buffers = mem_deref(st->buffers);
	st->n_buffers = 0;
}


static int start_capturing(struct vidsrc_st *st)
{
	unsigned int i;
	enum v4l2_buf_type type;

	for (i = 0; i < st->n_buffers; ++i) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));

		buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index  = i;

		if (-1 == xioctl (st->fd, VIDIOC_QBUF, &buf))
			return errno;
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (-1 == xioctl (st->fd, VIDIOC_STREAMON, &type))
		return errno;

	return 0;
}


static void call_frame_handler(struct vidsrc_st *st, uint8_t *buf,
			       uint64_t timestamp)
{
	struct vidframe frame;

	vidframe_init_buf(&frame, match_fmt(st->pixfmt), &st->sz, buf);

	st->frameh(&frame, timestamp, st->arg);
}


static int read_frame(struct vidsrc_st *st)
{
	struct v4l2_buffer buf;
	struct timeval ts;
	uint64_t timestamp;

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

	ts = buf.timestamp;
	timestamp = 1000000U * ts.tv_sec + ts.tv_usec;
	timestamp = timestamp * VIDEO_TIMEBASE / 1000000U;

	call_frame_handler(st, st->buffers[buf.index].start, timestamp);

	if (-1 == xioctl (st->fd, VIDIOC_QBUF, &buf)) {
		warning("v4l2: VIDIOC_QBUF\n");
		return errno;
	}

	return 0;
}


static int set_available_devices(struct list* dev_list)
{
	int i, fd;
	char name[16];
	int err;

	for (i=0;i < 16;i++) {

		re_snprintf(name, sizeof(name), "/dev/video%i", i);

		if ((fd = open(name, O_RDONLY)) == -1) {
			continue;
		}
		else {
			close(fd);
			err = mediadev_add(dev_list, name);
			if (err)
				return err;
		}
	}

	return 0;
}

static int vd_open(struct vidsrc_st *st, const char *device)
{
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

	debug("v4l2: stopping video source..\n");

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	stop_capturing(st);
	uninit_device(st);

	if (st->fd >= 0)
		v4l2_close(st->fd);
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


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	struct mediadev *md;
	int err;

	(void)ctx;
	(void)prm;
	(void)fmt;
	(void)errorh;

	if (!stp || !size || !frameh)
		return EINVAL;

	if (!str_isset(dev)) {
		md = mediadev_get_default(&vs->dev_list);
		if (md) {
			dev = md->name;
		}
		else {
			warning("v4l2: No available devices\n");
			return ENODEV;
		}
	}

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs = vs;
	st->fd = -1;
	st->sz = *size;
	st->frameh = frameh;
	st->arg    = arg;
	st->pixfmt = 0;

	err = vd_open(st, dev);
	if (err)
		goto out;

	err = v4l2_init_device(st, dev, size->w, size->h);
	if (err)
		goto out;

	print_video_input(st);

	print_framerate(st);

	err = start_capturing(st);
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
	int err;

	err = vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "v4l2", alloc, NULL);
	if (err)
		return err;

	list_init(&vidsrc->dev_list);
	err = set_available_devices(&vidsrc->dev_list);

	return err;
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
