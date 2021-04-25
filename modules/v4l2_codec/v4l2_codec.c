/**
 * @file v4l2_codec.c  Video4Linux2 video-source and video-codec
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#if defined (OPENBSD) || defined (NETBSD)
#include <sys/videoio.h>
#else
#include <linux/videodev2.h>
#endif


/**
 * @defgroup v4l2_codec v4l2_codec
 *
 * V4L2 (Video for Linux 2) video-codec and source hybrid module
 *
 * This module is using V4L2 (Video for Linux 2) as a codec module
 * for devices that supports compressed formats such as H.264.
 * The module implements both the vidsrc API and the vidcodec API.
 *
 */


struct vidsrc_st {
	uint8_t *buffer;
	size_t buffer_len;
	int fd;
	pthread_t thread;
	bool run;
	vidsrc_packet_h *packeth;
	void *arg;
};

static struct vidsrc *vidsrc;


static int xioctl(int fd, unsigned long int request, void *arg)
{
	int r;

	do r = ioctl (fd, request, arg);
	while (-1 == r && EINTR == errno);

	return r;
}


static int print_caps(int fd, unsigned width, unsigned height)
{
	struct v4l2_capability caps;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_format fmt;
	bool support_h264 = false;
	char fourcc[5] = {0};
	char c;
	int err;

	memset(&caps, 0, sizeof(caps));
	memset(&fmtdesc, 0, sizeof(fmtdesc));
	memset(&fmt, 0, sizeof(fmt));

	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps)) {
		err = errno;
		warning("v4l2_codec: error Querying Capabilities (%m)\n", err);
		return err;
	}

	info("v4l2_codec: Driver Caps:\n"
		"  Driver:        \"%s\"\n"
		"  Card:          \"%s\"\n"
		"  Bus:           \"%s\"\n"
		"  Version:       %d.%d\n"
		"  Capabilities:  0x%08x\n",
		caps.driver,
		caps.card,
		caps.bus_info,
		(caps.version>>16) & 0xff,
		(caps.version>>24) & 0xff,
		caps.capabilities);


	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	info("  Formats:\n");

	while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) {
		bool selected = false;

		strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);

#ifdef V4L2_PIX_FMT_H264
		if (fmtdesc.pixelformat == V4L2_PIX_FMT_H264) {
			support_h264 = true;
			selected = true;
		}
#endif

		c = fmtdesc.flags & V4L2_FMT_FLAG_COMPRESSED ? 'C' : ' ';

		info("  %c  %s: %c  '%s'\n",
		       selected ? '>' : ' ',
		       fourcc, c, fmtdesc.description);

		fmtdesc.index++;
	}

	info("\n");

	if (!support_h264) {
		warning("v4l2_codec: Doesn't support H264.\n");
		return ENODEV;
	}

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
#ifdef V4L2_PIX_FMT_H264
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
#endif
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt)) {
		err = errno;
		warning("v4l2_codec: Setting Pixel Format (%m)\n", err);
		return err;
	}

	strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
	info("v4l2_codec: Selected Camera Mode:\n"
		"  Width:   %d\n"
		"  Height:  %d\n"
		"  PixFmt:  %s\n"
		"  Field:   %d\n",
		fmt.fmt.pix.width,
		fmt.fmt.pix.height,
		fourcc,
		fmt.fmt.pix.field);

	return 0;
}


static int init_mmap(struct vidsrc_st *st, int fd)
{
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&buf, 0, sizeof(buf));

	req.count = 1;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
		err = errno;
		warning("v4l2_codec: Requesting Buffer (%m)\n", err);
		return err;
	}

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
		err = errno;
		warning("v4l2_codec: Querying Buffer (%m)\n", err);
		return err;
	}

	st->buffer = mmap(NULL, buf.length,
			  PROT_READ | PROT_WRITE, MAP_SHARED,
			  fd, buf.m.offset);
	if (st->buffer == MAP_FAILED) {
		err = errno;
		warning("v4l2_codec: mmap failed (%m)\n", err);
		return err;
	}
	st->buffer_len = buf.length;

	return 0;
}


static int query_buffer(int fd)
{
	struct v4l2_buffer buf;

	memset(&buf, 0, sizeof(buf));

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;

	if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
		return errno;

	return 0;
}


static int start_streaming(int fd)
{
	struct v4l2_buffer buf;
	int err;

	memset(&buf, 0, sizeof(buf));

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;

	if (-1 == xioctl(fd, VIDIOC_STREAMON, &buf.type)) {
		err = errno;
		warning("v4l2_codec: Start Capture (%m)\n", err);
		return err;
	}

	return 0;
}


static void stop_capturing(int fd)
{
	enum v4l2_buf_type type;

	if (fd < 0)
		return;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	xioctl(fd, VIDIOC_STREAMOFF, &type);
}


static void read_frame(struct vidsrc_st *st)
{
	struct v4l2_buffer buf;
	struct vidpacket vp;
	struct timeval ts;
	uint64_t timestamp;
	int err;

	memset(&buf, 0, sizeof(buf));

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;

	if (-1 == xioctl(st->fd, VIDIOC_DQBUF, &buf)) {
		err = errno;
		warning("v4l2_codec: Retrieving Frame (%m)\n", err);
		return;
	}


	ts = buf.timestamp;
	timestamp = 1000000*ts.tv_sec + ts.tv_usec;

	vp.buf = st->buffer;
	vp.size = buf.bytesused;
	vp.timestamp = timestamp;

#if 0
	debug("v4l2_codec: %s frame captured at %ldsec, %ldusec"
	      " (%zu bytes) rtp_ts=%llu\n",
	      keyframe ? "KEY" : "   ",
	      buf.timestamp.tv_sec, buf.timestamp.tv_usec,
	      (size_t)buf.bytesused, rtp_ts);
#endif

	if (st->packeth)
		st->packeth(&vp, st->arg);
	else {
		warning("v4l2_codec: no packet handler\n");
	}

	err = query_buffer(st->fd);
	if (err) {
		warning("v4l2_codec: query_buffer failed (%m)\n", err);
	}
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;

	while (st->run) {
		read_frame(st);
	}

	return NULL;
}


static int open_encoder(struct vidsrc_st *st, const char *device,
			unsigned width, unsigned height)
{
	int err;

	debug("v4l2_codec: opening video-encoder device (device=%s)\n",
	      device);

	st->fd = open(device, O_RDWR);
	if (st->fd == -1) {
		err = errno;
		warning("Opening video device (%m)\n", err);
		goto out;
	}

	err = print_caps(st->fd, width, height);
	if (err)
		goto out;

	err = init_mmap(st, st->fd);
	if (err)
		goto out;

	err = query_buffer(st->fd);
	if (err)
		goto out;

	err = start_streaming(st->fd);
	if (err)
		goto out;

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

out:
	return err;
}


static void src_destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	stop_capturing(st->fd);

	if (st->buffer)
		munmap(st->buffer, st->buffer_len);

	if (st->fd >= 0) {
		close(st->fd);
	}
}


static int src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		     struct media_ctx **ctx, struct vidsrc_prm *prm,
		     const struct vidsz *size, const char *fmt,
		     const char *dev, vidsrc_frame_h *frameh,
		     vidsrc_packet_h  *packeth,
		     vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err = 0;

	(void)vs;
	(void)ctx;
	(void)prm;
	(void)fmt;
	(void)errorh;

	if (!stp || !size || !frameh)
		return EINVAL;

	if (!str_isset(dev))
		dev = "/dev/video0";

	debug("v4l2_codec: video-source alloc (device=%s)\n", dev);

	st = mem_zalloc(sizeof(*st), src_destructor);
	if (!st)
		return ENOMEM;

	st->packeth = packeth;
	st->arg = arg;

	err = open_encoder(st, dev, size->w, size->h);
	if (err)
		goto out;

out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int module_init(void)
{
	info("v4l2_codec inited\n");

	return vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "v4l2_codec", src_alloc, NULL);
}


static int module_close(void)
{
	vidsrc = mem_deref(vidsrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(v4l2_codec) = {
	"v4l2_codec",
	"vidcodec",
	module_init,
	module_close
};
