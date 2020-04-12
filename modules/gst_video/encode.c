/**
 * @file gst_video/encode.c  Video codecs using Gstreamer video pipeline
 *
 * Copyright (C) 2010 - 2013 Creytiv.com
 * Copyright (C) 2014 Fadeev Alexander
 * Copyright (C) 2015 Thomas Strobel
 */

#define __USE_POSIX199309
#define _DEFAULT_SOURCE 1
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "gst_video.h"


struct videnc_state {

	struct {
		struct vidsz size;
		unsigned fps;
		unsigned bitrate;
		unsigned pktsize;
	} encoder;

	struct {
		uint32_t packetization_mode;
		uint32_t profile_idc;
		uint32_t profile_iop;
		uint32_t level_idc;
		uint32_t max_fs;
		uint32_t max_smbps;
	} h264;

	videnc_packet_h *pkth;
	void *arg;

	/* Gstreamer */
	struct {
		bool valid;

		GstElement *pipeline;
		GstAppSrc *source;

		GstAppSrcCallbacks appsrcCallbacks;
		GstAppSinkCallbacks appsinkCallbacks;

		struct {
			pthread_mutex_t mutex;
			pthread_cond_t cond;
			int flag;
		} eos;

		/* Thread synchronization. */
		struct {
			pthread_mutex_t mutex;
			pthread_cond_t cond;
			/* 0: no-wait, 1: wait, -1: pipeline destroyed */
			int flag;
		} wait;
	} streamer;
};


static void appsrc_need_data_cb(GstAppSrc *src, guint size, gpointer user_data)
{
	struct videnc_state *st = user_data;
	(void)src;
	(void)size;

	pthread_mutex_lock(&st->streamer.wait.mutex);
	if (st->streamer.wait.flag == 1){
		st->streamer.wait.flag = 0;
		pthread_cond_signal(&st->streamer.wait.cond);
	}
	pthread_mutex_unlock(&st->streamer.wait.mutex);
}


static void appsrc_enough_data_cb(GstAppSrc *src, gpointer user_data)
{
	struct videnc_state *st = user_data;
	(void)src;

	pthread_mutex_lock(&st->streamer.wait.mutex);
	if (st->streamer.wait.flag == 0)
		st->streamer.wait.flag = 1;
	pthread_mutex_unlock(&st->streamer.wait.mutex);
}


static void appsrc_destroy_notify_cb(struct videnc_state *st)
{
	pthread_mutex_lock(&st->streamer.wait.mutex);
	st->streamer.wait.flag = -1;
	pthread_cond_signal(&st->streamer.wait.cond);
	pthread_mutex_unlock(&st->streamer.wait.mutex);
}


/* The appsink has received a sample */
static GstFlowReturn appsink_new_sample_cb(GstAppSink *sink,
					   gpointer user_data)
{
	struct videnc_state *st = user_data;
	GstSample *sample;
	GstBuffer *buffer;
	GstMapInfo info;
	guint8 *data;
	gsize size;

	/* Retrieve the sample */
	sample = gst_app_sink_pull_sample(sink);

	if (sample) {
		GstClockTime ts;
		uint64_t rtp_ts;

		buffer = gst_sample_get_buffer(sample);
		gst_buffer_map( buffer, &info, (GstMapFlags)(GST_MAP_READ) );

		data = info.data;
		size = info.size;

		ts = GST_BUFFER_PTS(buffer);

		if (ts == GST_CLOCK_TIME_NONE) {
			warning("gst_video: timestamp is unknown\n");
			rtp_ts = 0;
		}
		else {
			/* convert from nanoseconds to RTP clock */
			rtp_ts = (uint64_t)((90000ULL * ts) / 1000000000UL);
		}

		h264_packetize(rtp_ts, data, size, st->encoder.pktsize,
			       st->pkth, st->arg);

		gst_buffer_unmap(buffer, &info);
		gst_sample_unref(sample);
	}

	return GST_FLOW_OK;
}


static void appsink_end_of_stream_cb(GstAppSink *src, gpointer user_data)
{
	struct videnc_state *st = user_data;
	(void)src;

	pthread_mutex_lock(&st->streamer.eos.mutex);
	if (st->streamer.eos.flag == 0) {
		st->streamer.eos.flag = 1;
		pthread_cond_signal(&st->streamer.eos.cond);
	}
	pthread_mutex_unlock(&st->streamer.eos.mutex);
}


static void appsink_destroy_notify_cb(struct videnc_state *st)
{
	pthread_mutex_lock(&st->streamer.eos.mutex);
	st->streamer.eos.flag = -1;
	pthread_cond_signal(&st->streamer.eos.cond);
	pthread_mutex_unlock(&st->streamer.eos.mutex);
}


static GstBusSyncReply bus_sync_handler_cb(GstBus *bus, GstMessage *msg,
					   struct videnc_state *st)
{
	(void)bus;

	if ((GST_MESSAGE_TYPE (msg)) == GST_MESSAGE_ERROR) {
		GError *err = NULL;
		gchar *dbg_info = NULL;
		gst_message_parse_error (msg, &err, &dbg_info);
		warning("gst_video: Error: %d(%m) message=%s\n",
			err->code, err->code, err->message);
		warning("gst_video: Debug: %s\n", dbg_info);
		g_error_free (err);
		g_free (dbg_info);

		/* mark pipeline as broked */
		st->streamer.valid = false;
	}

	gst_message_unref(msg);
	return GST_BUS_DROP;
}


static void bus_destroy_notify_cb(struct videnc_state *st)
{
	(void)st;
}


/**
 * Set up the Gstreamer pipeline. Appsrc gets raw frames, and appsink takes
 * encoded frames.
 *
 * The pipeline looks like this:
 *
 * <pre>
 *  .--------.   .-----------.   .----------.
 *  | appsrc |   |  x264enc  |   | appsink  |
 *  |   .----|   |----.  .---|   |----.     |
 *  |   |src |-->|sink|  |src|-->|sink|-----+-->handoff
 *  |   '----|   |----'  '---|   |----'     |   handler
 *  '--------'   '-----------'   '----------'
 * </pre>
 */
static int pipeline_init(struct videnc_state *st, const struct vidsz *size)
{
	GstAppSrc *source;
	GstAppSink *sink;
	GstBus *bus;
	GError* gerror = NULL;
	char pipeline[1024];
	GstStateChangeReturn ret;
	int err = 0;

	if (!st || !size)
		return EINVAL;

	snprintf(pipeline, sizeof(pipeline),
	 "appsrc name=source is-live=TRUE block=TRUE "
	 "do-timestamp=TRUE max-bytes=1000000 ! "
	 "videoparse width=%d height=%d format=i420 framerate=%d/1 ! "
	 "x264enc byte-stream=TRUE rc-lookahead=0 "
	 "tune=zerolatency speed-preset=ultrafast "
	 "sync-lookahead=0 bitrate=%d ! "
	 "appsink name=sink emit-signals=TRUE drop=TRUE",
	 size->w, size->h,
	 st->encoder.fps, st->encoder.bitrate / 1000 /* kbit/s */);

	/* Initialize pipeline. */
	st->streamer.pipeline = gst_parse_launch(pipeline, &gerror);

	if (gerror) {
		warning("gst_video: launch error: %d: %s: %s\n",
			gerror->code, gerror->message, pipeline);
		err = gerror->code;
		g_error_free(gerror);
		return err;
	}

	/* Configure appsource */
	source = GST_APP_SRC(gst_bin_get_by_name(
				 GST_BIN(st->streamer.pipeline), "source"));
	gst_app_src_set_callbacks(source, &(st->streamer.appsrcCallbacks),
			  st, (GDestroyNotify)appsrc_destroy_notify_cb);

	/* Configure appsink. */
	sink = GST_APP_SINK(gst_bin_get_by_name(
				GST_BIN(st->streamer.pipeline), "sink"));
	gst_app_sink_set_callbacks(sink, &(st->streamer.appsinkCallbacks),
			   st, (GDestroyNotify)appsink_destroy_notify_cb);
	gst_object_unref(GST_OBJECT(sink));

	/* Bus watch */
	bus = gst_pipeline_get_bus(GST_PIPELINE(st->streamer.pipeline));
	gst_bus_set_sync_handler(bus, (GstBusSyncHandler)bus_sync_handler_cb,
				 st, (GDestroyNotify)bus_destroy_notify_cb);
	gst_object_unref(GST_OBJECT(bus));

	/* Set start values of locks */
	pthread_mutex_lock(&st->streamer.wait.mutex);
	st->streamer.wait.flag = 0;
	pthread_mutex_unlock(&st->streamer.wait.mutex);

	pthread_mutex_lock(&st->streamer.eos.mutex);
	st->streamer.eos.flag = 0;
	pthread_mutex_unlock(&st->streamer.eos.mutex);

	/* Start pipeline */
	ret = gst_element_set_state(st->streamer.pipeline, GST_STATE_PLAYING);
	if (GST_STATE_CHANGE_FAILURE == ret) {
		g_warning("set state returned GST_STATE_CHANGE_FAILURE\n");
		err = EPROTO;
		goto out;
	}

	st->streamer.source = source;

	/* Mark pipeline as working */
	st->streamer.valid = true;

 out:
	return err;
}


static void pipeline_close(struct videnc_state *st)
{
	if (!st)
		return;

	st->streamer.valid = false;

	if (st->streamer.source) {
		gst_object_unref(GST_OBJECT(st->streamer.source));
		st->streamer.source = NULL;
	}

	if (st->streamer.pipeline) {
		gst_element_set_state(st->streamer.pipeline, GST_STATE_NULL);

		/* pipeline */
		gst_object_unref(GST_OBJECT(st->streamer.pipeline));
		st->streamer.pipeline = NULL;
	}
}


static void destruct_resources(void *data)
{
	struct videnc_state *st = data;

	/* close pipeline */
	pipeline_close(st);

	/* destroy locks */
	pthread_mutex_destroy(&st->streamer.eos.mutex);
	pthread_cond_destroy(&st->streamer.eos.cond);

	pthread_mutex_destroy(&st->streamer.wait.mutex);
	pthread_cond_destroy(&st->streamer.wait.cond);
}


static int allocate_resources(struct videnc_state **stp)
{
	struct videnc_state *st;

	st = mem_zalloc(sizeof(*st), destruct_resources);
	if (!st)
		return ENOMEM;

	*stp = st;

	/* initialize locks */
	pthread_mutex_init(&st->streamer.eos.mutex, NULL);
	pthread_cond_init(&st->streamer.eos.cond, NULL);

	pthread_mutex_init(&st->streamer.wait.mutex, NULL);
	pthread_cond_init(&st->streamer.wait.cond, NULL);


	/* Set appsource callbacks. */
	st->streamer.appsrcCallbacks.need_data = &appsrc_need_data_cb;
	st->streamer.appsrcCallbacks.enough_data = &appsrc_enough_data_cb;

	/* Set appsink callbacks. */
	st->streamer.appsinkCallbacks.new_sample = &appsink_new_sample_cb;
	st->streamer.appsinkCallbacks.eos = &appsink_end_of_stream_cb;

	return 0;
}


/*
  decode sdpparameter for h264
*/
static void param_handler(const struct pl *name, const struct pl *val,
			  void *arg)
{
	struct videnc_state *st = arg;

	if (0 == pl_strcasecmp(name, "packetization-mode")) {
		st->h264.packetization_mode = pl_u32(val);

		if (st->h264.packetization_mode != 0) {
			warning("gst_video: illegal packetization-mode %u\n",
				st->h264.packetization_mode);
			return;
		}
	}
	else if (0 == pl_strcasecmp(name, "profile-level-id")) {
		struct pl prof = *val;
		if (prof.l != 6) {
			warning("gst_video: invalid profile-level-id (%r)\n",
				val);
			return;
		}

		prof.l = 2;
		st->h264.profile_idc = pl_x32(&prof); prof.p += 2;
		st->h264.profile_iop = pl_x32(&prof); prof.p += 2;
		st->h264.level_idc   = pl_x32(&prof);
	}
	else if (0 == pl_strcasecmp(name, "max-fs")) {
		st->h264.max_fs = pl_u32(val);
	}
	else if (0 == pl_strcasecmp(name, "max-smbps")) {
		st->h264.max_smbps = pl_u32(val);
	}

	return;
}


int gst_video_encoder_set(struct videnc_state **stp,
			  const struct vidcodec *vc,
			  struct videnc_param *prm, const char *fmtp,
			  videnc_packet_h *pkth, void *arg)
{
	struct videnc_state *st;
	int err = 0;

	if (!stp || !vc || !prm || !pkth)
		return EINVAL;

	st = *stp;

	if (!st) {
		err = allocate_resources(stp);
		if (err) {
			warning("gst_video: resource allocation failed\n");
			return err;
		}
		st = *stp;

		st->pkth = pkth;
		st->arg = arg;
	}
	else {
		if (!st->streamer.valid) {
			warning("gst_video codec: trying to work"
				" with invalid pipeline\n");
			return EINVAL;
		}

		if ((st->encoder.bitrate != prm->bitrate ||
	             st->encoder.pktsize != prm->pktsize ||
	             st->encoder.fps     != prm->fps)) {

			pipeline_close(st);
		}
	}

	st->encoder.bitrate = prm->bitrate;
	st->encoder.pktsize = prm->pktsize;
	st->encoder.fps     = prm->fps;

	if (str_isset(fmtp)) {
		struct pl sdp_fmtp;
		pl_set_str(&sdp_fmtp, fmtp);

		/* store new parameters */
		fmt_param_apply(&sdp_fmtp, param_handler, st);
	}

	info("gst_video: video encoder %s: %d fps, %d bit/s, pktsize=%u\n",
	     vc->name, st->encoder.fps,
	     st->encoder.bitrate, st->encoder.pktsize);

	return err;
}


/*
 * couple gstreamer tightly by lock-stepping
 */
static int pipeline_push(struct videnc_state *st, const struct vidframe *frame,
			 uint64_t timestamp)
{
	GstBuffer *buffer;
	uint8_t *data;
	size_t size;
	GstFlowReturn ret;
	int err = 0;

	/*
	 * Wait "start feed".
	 */
	pthread_mutex_lock(&st->streamer.wait.mutex);
	if (st->streamer.wait.flag == 1) {
		pthread_cond_wait(&st->streamer.wait.cond,
				  &st->streamer.wait.mutex);
	}
	if (st->streamer.eos.flag == -1)
		/* error */
		err = ENODEV;
	pthread_mutex_unlock(&st->streamer.wait.mutex);
	if (err)
		return err;

	/*
	 * Copy frame into buffer for gstreamer
	 */

	/* NOTE: I420 (YUV420P): hardcoded. */
	size = frame->linesize[0] * frame->size.h
		+ frame->linesize[1] * frame->size.h * 0.5
		+ frame->linesize[2] * frame->size.h * 0.5;

	/* allocate memory; memory is freed within callback of
	   gst_memory_new_wrapped of gst_video_push */
	data = g_try_malloc(size);
	if (!data)
		return ENOMEM;

	/* copy content of frame */
	size = 0;
	memcpy(&data[size], frame->data[0],
	       frame->linesize[0] * frame->size.h);
	size += frame->linesize[0] * frame->size.h;
	memcpy(&data[size], frame->data[1],
	       frame->linesize[1] * frame->size.h * 0.5);
	size += frame->linesize[1] * frame->size.h * 0.5;
	memcpy(&data[size], frame->data[2],
	       frame->linesize[2] * frame->size.h * 0.5);
	size += frame->linesize[2] * frame->size.h * 0.5;

	/* Wrap memory in a gstreamer buffer */
	buffer = gst_buffer_new();
	gst_buffer_insert_memory(buffer, -1,
				 gst_memory_new_wrapped (0, data, size, 0,
							 size, data, g_free));

	/* convert timestamp to nanoseconds */
	buffer->pts = timestamp * 1000000000ULL / VIDEO_TIMEBASE;

	/*
	 * Push data and EOS into gstreamer.
	 */

	ret = gst_app_src_push_buffer(st->streamer.source, buffer);
	if (ret != GST_FLOW_OK) {
		warning("gst_video: pushing buffer failed\n");
		err = EPROTO;
		goto out;
	}

#if 0
	ret = gst_app_src_end_of_stream(st->streamer.source);
	if (ret != GST_FLOW_OK) {
		warning("gst_video: pushing EOS failed\n");
		err = EPROTO;
		goto out;
	}
#endif


#if 0
	/*
	 * Wait "processing done".
	 */
	pthread_mutex_lock(&st->streamer.eos.mutex);
	if (st->streamer.eos.flag == 0)
		/* will returns with EOS (1) or error (-1) */
		pthread_cond_wait(&st->streamer.wait.cond,
				  &st->streamer.wait.mutex);
	if (st->streamer.eos.flag == 1)
		/* reset eos */
		st->streamer.eos.flag = 0;
	else
		/* error */
		err = -1;
	pthread_mutex_unlock(&st->streamer.wait.mutex);
#endif


 out:
	return err;
}


int gst_video_encode(struct videnc_state *st, bool update,
		      const struct vidframe *frame, uint64_t timestamp)
{
	int err;

	if (!st || !frame || frame->fmt != VID_FMT_YUV420P)
		return EINVAL;

	if (!st->streamer.valid ||
	    !vidsz_cmp(&st->encoder.size, &frame->size)) {

		pipeline_close(st);

		err = pipeline_init(st, &frame->size);
		if (err) {
			warning("gst_video: pipeline initialization failed\n");
			return err;
		}

		st->encoder.size = frame->size;
	}

	if (update) {
		debug("gst_video: gstreamer picture update"
		      ", it's not implemented...\n");
	}

	/*
	 * Push frame into pipeline.
	 * Function call will return once frame has been processed completely.
	 */
	err = pipeline_push(st, frame, timestamp);

	return err;
}
