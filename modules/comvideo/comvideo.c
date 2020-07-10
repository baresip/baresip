/**
 * @file comvideo.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

/**
 * @defgroup comvideo comvideo
 *
 * Commend specific video source and h264 codec implementation using
 * GStreamer video codecs.
 *
 * The video stream is captured from the camerad process via a DBus interface.
 *
 \verbatim
  comvideo_camerad_dbus_name com.commend.camerad.Service # camerad DBus name
  comvideo_camerad_dbus_path /commend                    # camerad DBus path
 \endverbatim
 *
 * References:
 *
 *
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <glib.h>
#include "comvideo.h"

#define MODULE_NAME                 "comvideo"

#define PROPERTY_VIDEO_DBUS_NAME    "comvideo_video_dbus_name"
#define DEFAULT_VIDEO_DBUS_NAME     "com.commend.videoserver.Service"

#define PROPERTY_VIDEO_DBUS_PATH    "comvideo_video_debus_path"
#define DEFAULT_VIDEO_DBUS_PATH     "/commend"

#define PROPERTY_CAMERAD_DBUS_NAME  "comvideo_camerad_dbus_name"
#define DEFAULT_CAMERAD_DBUS_NAME   "com.commend.camerad.Service"

#define PROPERTY_CAMERAD_DBUS_PATH  "comvideo_camerad_dbus_path"
#define DEFAULT_CAMERAD_DBUS_PATH   "/commend"

struct comvideo_data  comvideo_codec;

static struct vidsrc *vid_src;
static struct vidisp *vid_disp;

static bool h264_fmtp_cmp(const char *fmtp1, const char *fmtp2, void *data);

static int h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			 bool offer, void *arg);


static struct vidcodec h264 = {
	LE_INIT,
	NULL,
	"H264",
	"packetization-mode=0",
	NULL,
	encode_h264_update,
	encode_h264,
	decode_h264_update,
	decode_h264,
	h264_fmtp_enc,
	h264_fmtp_cmp,
};


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */
	struct vidsz sz;
	u_int32_t pixfmt;

	GstCameraSrc *camsrc;
	GList *encoders;

	struct buffer *buffers;
	vidsrc_frame_h *frameh;
	void *arg;
};


struct vidisp_st {
	const struct vidisp *vd;        /**< Inheritance (1st)     */
	struct vidsz size;              /**< Current size          */
};


static uint32_t packetization_mode(const char *fmtp) {
	struct pl pl, mode;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "packetization-mode", &mode))
		return pl_u32(&mode);

	return 0;
}


static bool h264_fmtp_cmp(const char *fmtp1, const char *fmtp2, void *data) {
	(void) data;
	return packetization_mode(fmtp1) == packetization_mode(fmtp2);
}


static int h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			 bool offer, void *arg) {
	struct vidcodec *vc = arg;
	const uint8_t profile_idc = 0x42; /* baseline profile */
	const uint8_t profile_iop = 0x80;
	static const uint8_t h264_level_idc = 0x0c;
	(void) offer;

	if (!mb || !fmt || !vc)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s"
			       " packetization-mode=0"
			       ";profile-level-id=%02x%02x%02x"
			       "\r\n",
			   fmt->id, profile_idc, profile_iop, h264_level_idc);
}


static void src_destructor(void *arg) {
	debug("comvideo: stopping video source..\n");

	struct vidsrc_st *st = arg;

	GstCameraSrc *src = st->camsrc;

	if (src) {
		gst_camera_src_set_sample_cb(src, NULL, NULL);

		if (comvideo_codec.camerad_client) {
			camerad_client_remove_src(
				comvideo_codec.camerad_client,
				src);
		}

		g_object_unref(src);
	}
}


static int src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		     struct media_ctx **ctx, struct vidsrc_prm *prm,
		     const struct vidsz *size, const char *fmt,
		     const char *dev, vidsrc_frame_h *frameh,
		     vidsrc_error_h *errorh, void *arg) {
	(void) ctx;
	(void) prm;
	(void) fmt;
	(void) errorh;

	struct vidsrc_st *st;
	GstCameraSrc *src;

	if (!stp || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), src_destructor);
	if (!st)
		return ENOMEM;

	st->vs = vs;
	st->sz = *size;
	st->frameh = frameh;
	st->arg = arg;
	st->pixfmt = 1;

	src = camerad_client_add_src(comvideo_codec.camerad_client,
				     GST_CAMERA_SRC_CODEC_H264, st->sz.w,
				     st->sz.h,
				     15);

	if (src) {
		gst_camera_src_set_sample_cb(
			src,
			(camera_new_sample) camera_h264_sample_received,
			st);
	}

	st->camsrc = src;
	*stp = st;

	return 0;
}


static void disp_destructor(void *arg) {
}


static int disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
		      struct vidisp_prm *prm, const char *dev,
		      vidisp_resize_h *resizeh, void *arg) {
	struct vidisp_st *st;
	int err = 0;
	(void) dev;
	(void) resizeh;
	(void) arg;

	st = mem_zalloc(sizeof(*st), disp_destructor);
	if (!st)
		return ENOMEM;

	*stp = st;

	return err;
}


static int module_init(void) {
	if (!gst_is_initialized()) {
		gst_init(NULL, NULL);
	}

	struct conf *conf = conf_cur();

	if (conf_get_str(conf, PROPERTY_VIDEO_DBUS_NAME,
			 comvideo_codec.video_dbus_name, DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.video_dbus_name,
			  DEFAULT_VIDEO_DBUS_NAME,
			  DBUS_PROPERTY_SIZE);
	}

	if (conf_get_str(conf, PROPERTY_VIDEO_DBUS_PATH,
			 comvideo_codec.video_dbus_path, DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.video_dbus_path,
			  DEFAULT_VIDEO_DBUS_PATH,
			  DBUS_PROPERTY_SIZE);
	}

	comvideo_codec.video_client =
		gst_video_client_new(
			comvideo_codec.video_dbus_name,
			comvideo_codec.video_dbus_path);

	if (conf_get_str(conf, PROPERTY_CAMERAD_DBUS_NAME,
			 comvideo_codec.camerad_dbus_name,
			 DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.camerad_dbus_name,
			  DEFAULT_CAMERAD_DBUS_NAME,
			  DBUS_PROPERTY_SIZE);
	}

	if (conf_get_str(conf, PROPERTY_CAMERAD_DBUS_PATH,
			 comvideo_codec.camerad_dbus_path,
			 DBUS_PROPERTY_SIZE)) {
		g_strlcpy(comvideo_codec.camerad_dbus_path,
			  DEFAULT_CAMERAD_DBUS_PATH,
			  DBUS_PROPERTY_SIZE);
	}

	comvideo_codec.camerad_client =
		camerad_client_new(
			comvideo_codec.camerad_dbus_name,
			comvideo_codec.camerad_dbus_path);

	vidcodec_register(baresip_vidcodecl(), &h264);

	vidisp_register(&vid_disp, baresip_vidispl(),
			MODULE_NAME, disp_alloc, NULL, NULL, NULL);

	return vidsrc_register(&vid_src, baresip_vidsrcl(),
			       MODULE_NAME, src_alloc, NULL);

	info(MODULE_NAME" inited\n");
}


static int module_close(void) {
	vid_src = mem_deref(vid_src);
	vid_disp = mem_deref(vid_disp);
	vidcodec_unregister(&h264);

	g_object_unref(comvideo_codec.camerad_client);
	g_object_unref(comvideo_codec.video_client);

	return 0;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(comvideo) = {
	MODULE_NAME,
	"vidcodec",
	module_init,
	module_close
};
