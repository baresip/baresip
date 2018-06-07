/**
 * @file gst_video/encode.c  Video codecs using Gstreamer video pipeline
 *
 * Copyright (C) 2010 - 2013 Creytiv.com
 * Copyright (C) 2014 Fadeev Alexander
 */
#define _DEFAULT_SOURCE 1
#define __USE_POSIX199309
#define _BSD_SOURCE 1
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
#include "gst_video.h"


struct videnc_state {

	struct vidsz size;
	unsigned fps;
	unsigned bitrate;
	unsigned pktsize;

	struct {
		uint32_t packetization_mode;
		uint32_t profile_idc;
		uint32_t profile_iop;
		uint32_t level_idc;
		uint32_t max_fs;
		uint32_t max_smbps;
	} h264;

	videnc_packet_h *pkth;
	void *pkth_arg;

	/* Gstreamer */
	GstElement *pipeline, *source, *sink;
	GstBus *bus;
	gulong need_data_handler;
	gulong enough_data_handler;
	gulong new_buffer_handler;
	bool gst_inited;

	/* Main loop thread. */
	int run;
	pthread_t tid;

	/* Thread synchronization. */
	pthread_mutex_t mutex;
	pthread_cond_t wait;
	int bwait;
};


static void gst_encoder_close(struct videnc_state *st);


static void internal_bus_watch_handler(struct videnc_state *st)
{
	GError *err;
	gchar *d;
	GstMessage *msg = gst_bus_pop(st->bus);

	if (!msg) {
		/* take a nap (300ms) */
		usleep(300 * 1000);
		return;
	}

	switch (GST_MESSAGE_TYPE(msg)) {

	case GST_MESSAGE_EOS:

		/* XXX decrementing repeat count? */

		/* Re-start stream */
		gst_element_set_state(st->pipeline, GST_STATE_NULL);
		gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
		break;

	case GST_MESSAGE_ERROR:
		gst_message_parse_error(msg, &err, &d);

		warning("gst_video: Error: %d(%m) message=%s\n", err->code,
			err->code, err->message);
		warning("gst_video: Debug: %s\n", d);

		g_free(d);
		g_error_free(err);

		st->run = FALSE;
		break;

	default:
		break;
	}

	gst_message_unref(msg);
}


static void *internal_thread(void *arg)
{
	struct videnc_state *st = arg;

	/* Now set to playing and iterate. */
	debug("gst_video: Setting pipeline to PLAYING\n");

	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);

	while (st->run) {
		internal_bus_watch_handler(st);
	}

	debug("gst_video: Pipeline thread was stopped.\n");

	return NULL;
}


static void internal_appsrc_start_feed(GstElement * pipeline, guint size,
				       struct videnc_state *st)
{
	(void)pipeline;
	(void)size;

	if (!st)
		return;

	pthread_mutex_lock(&st->mutex);
	st->bwait = FALSE;
	pthread_cond_signal(&st->wait);
	pthread_mutex_unlock(&st->mutex);
}


static void internal_appsrc_stop_feed(GstElement * pipeline,
				      struct videnc_state *st)
{
	(void)pipeline;

	if (!st)
		return;

	pthread_mutex_lock(&st->mutex);
	st->bwait = TRUE;
	pthread_mutex_unlock(&st->mutex);
}


/* The appsink has received a buffer */
static void internal_appsink_new_buffer(GstElement *sink,
					struct videnc_state *st)
{
	GstBuffer *buffer;

	if (!st)
		return;

	/* Retrieve the buffer */
	g_signal_emit_by_name(sink, "pull-buffer", &buffer);

	if (buffer) {
		GstClockTime ts;
		uint64_t rtp_ts;

		guint8 *data = GST_BUFFER_DATA(buffer);
		guint size = GST_BUFFER_SIZE(buffer);

		ts = GST_BUFFER_TIMESTAMP(buffer);

		rtp_ts = (uint64_t)((90000ULL*ts) / 1000000000UL );

		h264_packetize(rtp_ts, data, size, st->pktsize,
			       st->pkth, st->pkth_arg);

		gst_buffer_unref(buffer);
	}
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
static int gst_encoder_init(struct videnc_state *st, int width, int height,
			    int framerate, int bitrate)
{
	GError* gerror = NULL;
	char pipeline[1024];
	int err = 0;

	gst_encoder_close(st);

	snprintf(pipeline, sizeof(pipeline),
	 "appsrc name=source is-live=TRUE block=TRUE do-timestamp=TRUE ! "
	 "videoparse width=%d height=%d format=i420 framerate=%d/1 ! "
	 "x264enc byte-stream=TRUE rc-lookahead=0"
	 " sync-lookahead=0 bitrate=%d ! "
	 "appsink name=sink emit-signals=TRUE drop=TRUE",
	 width, height, framerate, bitrate / 1000 /* kbit/s */);

	debug("gst_video: format: yu12 = yuv420p = i420\n");

	/* Initialize pipeline. */
	st->pipeline = gst_parse_launch(pipeline, &gerror);
	if (gerror) {
		warning("gst_video: launch error: %s: %s\n",
			gerror->message, pipeline);
		err = gerror->code;
		g_error_free(gerror);
		goto out;
	}

	st->source = gst_bin_get_by_name(GST_BIN(st->pipeline), "source");
	st->sink   = gst_bin_get_by_name(GST_BIN(st->pipeline), "sink");
	if (!st->source || !st->sink) {
		warning("gst_video: failed to get source or sink"
			" pipeline elements\n");
		err = ENOMEM;
		goto out;
	}

	/* Configure appsource */
	st->need_data_handler = g_signal_connect(st->source, "need-data",
				 G_CALLBACK(internal_appsrc_start_feed), st);
	st->enough_data_handler = g_signal_connect(st->source, "enough-data",
				   G_CALLBACK(internal_appsrc_stop_feed), st);

	/* Configure appsink. */
	st->new_buffer_handler = g_signal_connect(st->sink, "new-buffer",
				  G_CALLBACK(internal_appsink_new_buffer), st);

	/********************* Misc **************************/

	/* Bus watch */
	st->bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));

	/********************* Thread **************************/

	/* Synchronization primitives. */
	pthread_mutex_init(&st->mutex, NULL);
	pthread_cond_init(&st->wait, NULL);
	st->bwait = FALSE;

	err = gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
	if (GST_STATE_CHANGE_FAILURE == err) {
		g_warning("set state returned GST_STATE_CHANGE_FAILUER\n");
	}

	/* Launch thread with gstreamer loop. */
	st->run = true;
	err = pthread_create(&st->tid, NULL, internal_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	st->gst_inited = true;

 out:
	return err;
}


static int gst_video_push(struct videnc_state *st, const uint8_t *src,
			  size_t size, uint64_t timestamp)
{
	GstBuffer *buffer;
	int ret = 0;

	if (!st) {
		return EINVAL;
	}

	if (!size) {
		warning("gst_video: push: eos returned %d at %d\n",
			ret, __LINE__);
		gst_app_src_end_of_stream((GstAppSrc *)st->source);
		return ret;
	}

	/* Wait "start feed". */
	pthread_mutex_lock(&st->mutex);
	if (st->bwait) {
#define WAIT_TIME_SECONDS 5
		struct timespec ts;
		struct timeval tp;
		gettimeofday(&tp, NULL);
		ts.tv_sec  = tp.tv_sec;
		ts.tv_nsec = tp.tv_usec * 1000;
		ts.tv_sec += WAIT_TIME_SECONDS;
		/* Wait. */
		ret = pthread_cond_timedwait(&st->wait, &st->mutex, &ts);
		if (ETIMEDOUT == ret) {
			warning("gst_video: Raw frame is lost"
				" because of timeout\n");
			return ret;
		}
	}
	pthread_mutex_unlock(&st->mutex);

	/* Create a new empty buffer */
	buffer = gst_buffer_new();
	GST_BUFFER_MALLOCDATA(buffer) = (guint8 *)src;
	GST_BUFFER_SIZE(buffer) = (guint)size;
	GST_BUFFER_DATA(buffer) = GST_BUFFER_MALLOCDATA(buffer);

	buffer->timestamp = timestamp * 1000000000ULL / VIDEO_TIMEBASE;

	ret = gst_app_src_push_buffer((GstAppSrc *)st->source, buffer);

	if (ret != GST_FLOW_OK) {
		warning("gst_video: push buffer returned"
			" %d for %d bytes \n", ret, size);
		return ret;
	}

	return ret;
}


static void gst_encoder_close(struct videnc_state *st)
{
	if (!st)
		return;

	st->gst_inited = false;

	/* Remove asynchronous callbacks to prevent using gst_video_t
	   context ("st") after releasing. */
	if (st->source) {
		g_signal_handler_disconnect(st->source,
					    st->need_data_handler);
		g_signal_handler_disconnect(st->source,
					    st->enough_data_handler);
	}
	if (st->sink) {
		g_signal_handler_disconnect(st->sink, st->new_buffer_handler);
	}

	/* Stop thread. */
	if (st->run) {
		st->run = false;
		pthread_join(st->tid, NULL);
	}

	if (st->source) {
		gst_object_unref(GST_OBJECT(st->source));
		st->source = NULL;
	}
	if (st->sink) {
		gst_object_unref(GST_OBJECT(st->sink));
		st->sink = NULL;
	}
	if (st->bus) {
		gst_object_unref(GST_OBJECT(st->bus));
		st->bus = NULL;
	}

	if (st->pipeline) {
		gst_element_set_state(st->pipeline, GST_STATE_NULL);
		gst_object_unref(GST_OBJECT(st->pipeline));
		st->pipeline = NULL;
	}
}


static void encode_destructor(void *arg)
{
	struct videnc_state *st = arg;

	gst_encoder_close(st);
}


static int decode_sdpparam_h264(struct videnc_state *st, const struct pl *name,
				const struct pl *val)
{
	if (0 == pl_strcasecmp(name, "packetization-mode")) {
		st->h264.packetization_mode = pl_u32(val);

		if (st->h264.packetization_mode != 0) {
			warning("gst_video: illegal packetization-mode %u\n",
				st->h264.packetization_mode);
			return EPROTO;
		}
	}
	else if (0 == pl_strcasecmp(name, "profile-level-id")) {
		struct pl prof = *val;
		if (prof.l != 6) {
			warning("gst_video: invalid profile-level-id (%r)\n",
				val);
			return EPROTO;
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

	return 0;
}


static void param_handler(const struct pl *name, const struct pl *val,
			  void *arg)
{
	struct videnc_state *st = arg;

	(void)decode_sdpparam_h264(st, name, val);
}


int gst_video_encode_update(struct videnc_state **vesp,
			    const struct vidcodec *vc,
			    struct videnc_param *prm, const char *fmtp,
			    videnc_packet_h *pkth, void *arg)
{
	struct videnc_state *ves;
	int err = 0;

	if (!vesp || !vc || !prm)
		return EINVAL;

	ves = *vesp;

	if (!ves) {

		ves = mem_zalloc(sizeof(*ves), encode_destructor);
		if (!ves)
			return ENOMEM;

		*vesp = ves;
	}
	else {
		if (ves->gst_inited && (ves->bitrate != prm->bitrate ||
					ves->pktsize != prm->pktsize ||
					ves->fps     != prm->fps)) {
			gst_encoder_close(ves);
		}
	}

	if (str_isset(fmtp)) {
		struct pl sdp_fmtp;

		pl_set_str(&sdp_fmtp, fmtp);

		fmt_param_apply(&sdp_fmtp, param_handler, ves);
	}

	ves->bitrate = prm->bitrate;
	ves->pktsize = prm->pktsize;
	ves->fps     = prm->fps;
	ves->pkth    = pkth;
	ves->pkth_arg = arg;

	info("gst_video: video encoder %s: %.2f fps, %d bit/s, pktsize=%u\n",
	      vc->name, prm->fps, prm->bitrate, prm->pktsize);

	return err;
}


int gst_video_encode(struct videnc_state *st, bool update,
		     const struct vidframe *frame, uint64_t timestamp)
{
	uint8_t *data;
	size_t size;
	int height;
	int err;

	if (!st || !frame || frame->fmt != VID_FMT_YUV420P)
		return EINVAL;

	if (!st->gst_inited || !vidsz_cmp(&st->size, &frame->size)) {

		err = gst_encoder_init(st, frame->size.w, frame->size.h,
				     st->fps, st->bitrate);

		if (err) {
			warning("gst_video codec: gst_video_alloc failed\n");
			return err;
		}

		/* To detect if requested size was changed. */
		st->size = frame->size;
	}

	if (update) {
		debug("gst_video: gstreamer picture update"
		      ", it's not implemented...\n");
	}

	height = frame->size.h;

	/* NOTE: I420 (YUV420P): hardcoded. */
	size = frame->linesize[0] * height
		+ frame->linesize[1] * height * 0.5
		+ frame->linesize[2] * height * 0.5;

	data = malloc(size);    /* XXX: memory-leak ? */
	if (!data)
		return ENOMEM;

	size = 0;

	/* XXX: avoid memcpy here ? */
	memcpy(&data[size], frame->data[0], frame->linesize[0] * height);
	size += frame->linesize[0] * height;
	memcpy(&data[size], frame->data[1], frame->linesize[1] * height * 0.5);
	size += frame->linesize[1] * height * 0.5;
	memcpy(&data[size], frame->data[2], frame->linesize[2] * height * 0.5);
	size += frame->linesize[2] * height * 0.5;

	return gst_video_push(st, data, size, timestamp);
}
