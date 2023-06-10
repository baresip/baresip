/**
 * @file gst_video/gst_video.c  Generic video pipeline using Gstreamer 1.0
 *
 * Copyright (C) 2023 Sebastian Reimers
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

struct vidsrc_st {
	GstElement *pipeline;
	GstAppSinkCallbacks appsinkCallbacks;
	bool run;
	bool eos;
	void *arg;
	int err;
	vidsrc_error_h *errh;
	vidsrc_packet_h *packeth;
};

static struct vidsrc *vidsrc;

/**
 * @defgroup gst_video gst_video
 *
 * This module implements a generic video pipeline using Gstreamer 1.0
 */


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	gst_element_set_state(st->pipeline, GST_STATE_NULL);
	gst_object_unref(st->pipeline);
}


static GstBusSyncReply sync_handler(GstBus *bus, GstMessage *msg,
				    struct vidsrc_st *st)
{
	GError *err;
	gchar *d;

	(void)bus;

	switch (GST_MESSAGE_TYPE(msg)) {

	case GST_MESSAGE_EOS:
		st->run = false;
		st->eos = true;
		break;

	case GST_MESSAGE_ERROR:
		gst_message_parse_error(msg, &err, &d);

		warning("gst: Error: %d(%m) message=\"%s\"\n", err->code,
			err->code, err->message);
		warning("gst: Debug: %s\n", d);

		g_free(d);

		st->err = err->code;

		/* Call error handler */
		if (st->errh)
			st->errh(err->code, st->arg);

		g_error_free(err);

		st->run = false;
		break;

	default:
		break;
	}

	gst_message_unref(msg);
	return GST_BUS_DROP;
}


/* The appsink has received a sample */
static GstFlowReturn appsink_new_sample_cb(GstAppSink *sink,
					   gpointer user_data)
{
	struct vidsrc_st *st = user_data;
	GstSample *sample;
	GstBuffer *buffer;
	GstMapInfo info;
	GstClockTime ts;
	static struct vidpacket vp;

	sample = gst_app_sink_pull_sample(sink);
	if (!sample)
		return GST_FLOW_OK;

	buffer = gst_sample_get_buffer(sample);
	gst_buffer_map(buffer, &info, (GstMapFlags)(GST_MAP_READ));

	vp.buf	= info.data;
	vp.size = info.size;

	ts = GST_BUFFER_PTS(buffer);

	if (ts == GST_CLOCK_TIME_NONE) {
		warning("gst_video: timestamp is unknown\n");
		vp.timestamp = 0;
	}
	else {
		/* convert from nanoseconds to RTP clock */
		vp.timestamp = (uint64_t)((90000ULL * ts) / 1000000000UL);
	}

	st->packeth(&vp, st->arg);

	gst_buffer_unmap(buffer, &info);
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct vidsrc_prm *prm, const struct vidsz *size,
		 const char *fmt, const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_packet_h *packeth, vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	GstAppSink *sink;
	GstBus *bus;
	GError *gerror = NULL;
	int err	       = 0;

	(void)prm;
	(void)fmt;
	(void)frameh;
	(void)vs;
	(void)dev;

	if (!stp || !size)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	/* Set appsink callbacks. */
	st->appsinkCallbacks.new_sample = &appsink_new_sample_cb;

	st->errh    = errorh;
	st->packeth = packeth;
	st->arg	    = arg;

	/* Build the pipeline */
	st->pipeline = gst_parse_launch(
		"v4l2src device=/dev/video0 io-mode=dmabuf ! videorate ! "
		"'video/x-raw,format=NV16,width=1920,height=1080,framerate=25/"
		"1' ! mpph264enc ! appsink name=sink emit-signals=TRUE "
		"drop=TRUE",
		&gerror);

	if (gerror) {
		warning("gst_video: launch error: %d: %s\n", gerror->code,
			gerror->message);
		err = gerror->code;
		g_error_free(gerror);
		goto out;
	}

	/* Configure appsink. */
	sink = GST_APP_SINK(
		gst_bin_get_by_name(GST_BIN(st->pipeline), "sink"));
	gst_app_sink_set_callbacks(sink, &(st->appsinkCallbacks), st, NULL);
	gst_object_unref(GST_OBJECT(sink));

	/* Bus watch */
	bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));
	gst_bus_enable_sync_message_emission(bus);
	gst_bus_set_sync_handler(bus, (GstBusSyncHandler)sync_handler, st,
				 NULL);

	st->run = true;
	st->eos = false;

	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);

	gst_object_unref(bus);

out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int module_init(void)
{
	int err;

	/*TODO: check if already initialized */
	gst_init(NULL, NULL);

	err = vidsrc_register(&vidsrc, baresip_vidsrcl(), "gst_video", alloc,
			      NULL);
	if (err)
		return err;

	info("gst_video: using gstreamer (%s)\n", gst_version_string());

	return 0;
}


static int module_close(void)
{

	/*TODO: check if already deinit */
	gst_deinit();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(gst_video) = {
	"gst_video", "vidsrc", module_init, module_close};
