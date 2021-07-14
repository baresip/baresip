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

static struct vidcodec h264 = {
	.name      = "H264",
	.variant   = NULL,
	.encupdh   = encode_h264_update,
	.ench      = encode_h264,
	.decupdh   = decode_h264_update,
	.dech      = decode_h264,
	.fmtp_ench = comvideo_fmtp_enc,
	.fmtp_cmph = NULL
};


struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */
	struct vidsz sz;
	u_int32_t pixfmt;
	u_int32_t fps;
	u_int32_t bitrate;

	vidsrc_frame_h *frameh;
	void *arg;
};


struct vidisp_st {
	const struct vidisp *vd;        /**< Inheritance (1st)     */
	struct vidsz size;              /**< Current size          */
};


static void src_destructor(void *arg)
{
	struct vidsrc_st *st;
	GstCameraSrc *src;

	(void) arg;

	st = arg;
	src = comvideo_codec.camera_src;

	info("comvideo: begin destructor video source: %p source list: %p\n",
	     st, comvideo_codec.sources);

	comvideo_codec.sources = g_list_remove(comvideo_codec.sources, st);

	if (!comvideo_codec.sources && src) {
		gst_camera_src_set_sample_cb(
			src,
			GST_CAMERA_SRC_CODEC_H264,
			0,
			NULL, NULL);

		if (comvideo_codec.camerad_client) {
			camerad_client_remove_src(
				comvideo_codec.camerad_client,
				src);
		}

		g_object_unref(src);

		comvideo_codec.camera_src = NULL;
	}

	info("comvideo: end destructor video source: %p source list: %p\n",
	     st, comvideo_codec.sources);
}


static int src_alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		     struct media_ctx **ctx, struct vidsrc_prm *prm,
		     const struct vidsz *size,
		     const char *fmt, const char *dev,
		     vidsrc_frame_h *frameh,
		     vidsrc_packet_h  *packeth,
		     vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	GstCameraSrc *src;
	struct config *cfg;

	(void) dev;
	(void) ctx;
	(void) fmt;
	(void) packeth;
	(void) errorh;

	if (!stp || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), src_destructor);
	if (!st)
		return ENOMEM;

	info("comvideo: begin allocate src: %p source list: %p\n",
	     st, comvideo_codec.sources);

	cfg = conf_config();

	st->vs = vs;
	st->sz = *size;
	st->frameh = frameh;
	st->arg = arg;
	st->pixfmt = 1;
	st->fps = (u_int32_t) prm->fps;
	st->bitrate = cfg->video.bitrate;

	if(!comvideo_codec.camera_src) {
		src = camerad_client_add_src(comvideo_codec.camerad_client,
					     GST_CAMERA_COMPONENT_RTP, st->sz.w,
					     st->sz.h,
					     st->fps);

		if (src) {
			gst_camera_src_set_sample_cb(
				src,
				GST_CAMERA_SRC_CODEC_H264,
				st->bitrate,
				(camera_new_sample) camera_h264_sample_received,
				st);
		}

		comvideo_codec.camera_src = src;
	}

	comvideo_codec.sources = g_list_append(comvideo_codec.sources, st);
	*stp = st;

	info("comvideo: end allocate src: %p  source list: %p\n",
	     st, comvideo_codec.sources);
	return 0;
}


static void
update_client_stream(gboolean disp_enabled)
{
	comvideo_codec.disp_enabled = disp_enabled;

	if(comvideo_codec.client_stream) {
		g_object_set(
			comvideo_codec.client_stream,
			"enabled", disp_enabled,
			NULL);
	}
}


static void disp_destructor(void *arg)
{
	update_client_stream(FALSE);
	(void) arg;
}


static int disp_alloc(struct vidisp_st **stp, const struct vidisp *vd,
		      struct vidisp_prm *prm, const char *dev,
		      vidisp_resize_h *resizeh, void *arg)
{
	struct vidisp_st *st;
	int err = 0;

	(void) prm;
	(void) vd;
	(void) dev;
	(void) resizeh;
	(void) arg;

	st = mem_zalloc(sizeof(*st), disp_destructor);
	if (!st)
		return ENOMEM;

	*stp = st;

	update_client_stream(TRUE);

	return err;
}


static int module_init(void) {

	struct conf *conf;

	if (!gst_is_initialized()) {
		gst_init(NULL, NULL);
	}

	conf = conf_cur();

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

	comvideo_codec.disp_enabled = FALSE;
	comvideo_codec.camera_src = NULL;
	comvideo_codec.sources = NULL;
	comvideo_codec.encoders = NULL;
	comvideo_codec.client_stream = NULL;

	comvideo_codec.camerad_client =
		camerad_client_new(
			comvideo_codec.camerad_dbus_name,
			comvideo_codec.camerad_dbus_path);

	vidcodec_register(baresip_vidcodecl(), &h264);

	vidisp_register(&vid_disp, baresip_vidispl(),
			MODULE_NAME, disp_alloc, NULL, NULL, NULL);

	return vidsrc_register(&vid_src, baresip_vidsrcl(),
			       MODULE_NAME, src_alloc, NULL);
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
