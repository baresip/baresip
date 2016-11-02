/**
 * @file v4l2_codec.c  Video4Linux2 video-source and video-codec
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
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
 *
 * TODO:
 *
 * - timestamp syncronization
 * - how to configure the wanted bitrate and framerate
 * - how to handle Key-frame requests
 */


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */

	uint8_t *buffer;
	size_t buffer_len;
	int fd;
	struct {
		unsigned n_key;
		unsigned n_delta;
	} stats;
};

struct videnc_state {
	struct le le;
	struct videnc_param encprm;
	videnc_packet_h *pkth;
	void *arg;
};


/* TODO: global data, move to per vidsrc instance */
static struct {
	/* List of encoder-states (struct videnc_state) */
	struct list encoderl;
} v4l2;


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


static void enc_destructor(void *arg)
{
	struct videnc_state *st = arg;

	list_unlink(&st->le);
}


static void encoders_read(const uint8_t *buf, size_t sz)
{
	struct le *le;
	int err;

	for (le = v4l2.encoderl.head; le; le = le->next) {
		struct videnc_state *st = le->data;

		err = h264_packetize(buf, sz,
				     st->encprm.pktsize, st->pkth, st->arg);
		if (err) {
			warning("h264_packetize error (%m)\n", err);
		}
	}
}


static void read_handler(int flags, void *arg)
{
	struct vidsrc_st *st = arg;
	struct v4l2_buffer buf;
	int err;
	(void)flags;

	memset(&buf, 0, sizeof(buf));

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;

	if (-1 == xioctl(st->fd, VIDIOC_DQBUF, &buf)) {
		err = errno;
		warning("v4l2_codec: Retrieving Frame (%m)\n", err);
		return;
	}

#if 0
	debug("image captured at %ld, %ld (%zu bytes)\n",
	      buf.timestamp.tv_sec, buf.timestamp.tv_usec,
	      (size_t)buf.bytesused);
#endif

	{
		struct mbuf mb = {0,0,0,0};
		struct h264_hdr hdr;

		mb.buf = st->buffer;
		mb.pos = 4;
		mb.end = buf.bytesused - 4;
		mb.size = buf.bytesused;

		err = h264_hdr_decode(&hdr, &mb);
		if (err) {
			warning("could not decode H.264 header\n");
		}
		else {
			if (h264_is_keyframe(hdr.type))
				++st->stats.n_key;
			else
				++st->stats.n_delta;
		}
	}

	/* pass the frame to the encoders */
	encoders_read(st->buffer, buf.bytesused);

	query_buffer(st->fd);
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

	err = fd_listen(st->fd, FD_READ, read_handler, st);
	if (err)
		goto out;

out:
	return err;
}


static int encode_update(struct videnc_state **vesp, const struct vidcodec *vc,
			 struct videnc_param *prm, const char *fmtp,
			 videnc_packet_h *pkth, void *arg)
{
	struct videnc_state *st;
	int err = 0;
	(void)fmtp;

	if (!vesp || !vc || !prm || !pkth)
		return EINVAL;

	if (*vesp)
		return 0;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	st->encprm = *prm;
	st->pkth = pkth;
	st->arg = arg;

	list_append(&v4l2.encoderl, &st->le, st);

	info("v4l2_codec: video encoder %s: %d fps, %d bit/s, pktsize=%u\n",
	      vc->name, prm->fps, prm->bitrate, prm->pktsize);

	if (err)
		mem_deref(st);
	else
		*vesp = st;

	return err;
}


/* note: dummy function, the input is unused */
static int encode_packet(struct videnc_state *st, bool update,
			 const struct vidframe *frame)
{
	(void)st;
	(void)update;
	(void)frame;
	return 0;
}


static uint32_t packetization_mode(const char *fmtp)
{
	struct pl pl, mode;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "packetization-mode", &mode))
		return pl_u32(&mode);

	return 0;
}


static int h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			 bool offer, void *arg)
{
	struct vidcodec *vc = arg;
	const uint8_t profile_idc = 0x42; /* baseline profile */
	const uint8_t profile_iop = 0x80;
	static const uint8_t h264_level_idc = 0x0c;
	(void)offer;

	if (!mb || !fmt || !vc)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s"
			   " packetization-mode=0"
			   ";profile-level-id=%02x%02x%02x"
			   "\r\n",
			   fmt->id, profile_idc, profile_iop, h264_level_idc);
}


static bool h264_fmtp_cmp(const char *fmtp1, const char *fmtp2, void *data)
{
	(void)data;

	return packetization_mode(fmtp1) == packetization_mode(fmtp2);
}


static void src_destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->fd >=0 ) {
		info("v4l2_codec: encoder stats"
		     " (keyframes:%u, deltaframes:%u)\n",
		     st->stats.n_key, st->stats.n_delta);
	}

	stop_capturing(st->fd);

	if (st->buffer)
		munmap(st->buffer, st->buffer_len);

	if (st->fd >= 0) {
		fd_close(st->fd);
		close(st->fd);
	}
}


static int src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		     struct media_ctx **ctx, struct vidsrc_prm *prm,
		     const struct vidsz *size, const char *fmt,
		     const char *dev, vidsrc_frame_h *frameh,
		     vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err = 0;

	(void)ctx;
	(void)prm;
	(void)fmt;
	(void)errorh;
	(void)arg;

	if (!stp || !size || !frameh)
		return EINVAL;

	if (str_isset(dev))
		dev = "/dev/video0";

	debug("v4l2_codec: video-source alloc (device=%s)\n", dev);

	st = mem_zalloc(sizeof(*st), src_destructor);
	if (!st)
		return ENOMEM;

	st->vs = vs;

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


static struct vidcodec h264 = {
	LE_INIT,
	NULL,
	"H264",
	"packetization-mode=0",
	NULL,
	encode_update,
	encode_packet,
	NULL,
	NULL,
	h264_fmtp_enc,
	h264_fmtp_cmp,
};


static int module_init(void)
{
	info("v4l2_codec inited\n");

	vidcodec_register(&h264);
	return vidsrc_register(&vidsrc, "v4l2_codec", src_alloc, NULL);
}


static int module_close(void)
{
	vidsrc = mem_deref(vidsrc);
	vidcodec_unregister(&h264);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(v4l2_codec) = {
	"v4l2_codec",
	"vidcodec",
	module_init,
	module_close
};
