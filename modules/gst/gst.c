/**
 * @file gst/gst.c  Gstreamer 1.0 playbin pipeline
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 199309L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gst/gst.h>
#include <unistd.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup gst gst
 *
 * Audio source module using gstreamer 1.0 as input
 *
 * The module 'gst' is using the Gstreamer framework to play external
 * media and provide this as an internal audio source.
 *
 * Example config:
 \verbatim
  audio_source        gst,http://relay.slayradio.org:8000/
 \endverbatim
 */


/**
 * Defines the Gstreamer state
 *
 * <pre>
 *                ptime=variable             ptime=20ms
 *  .-----------. N kHz          .---------. N kHz
 *  |           | 1-2 channels   |         | 1-2 channels
 *  | Gstreamer |--------------->|Packetize|-------------> [read handler]
 *  |           |                |         |
 *  '-----------'                '---------'
 *
 * </pre>
 */
struct ausrc_st {
	bool run;                   /**< Running flag            */
	bool eos;                   /**< Reached end of stream   */
	ausrc_read_h *rh;           /**< Read handler            */
	ausrc_error_h *errh;        /**< Error handler           */
	void *arg;                  /**< Handler argument        */
	struct ausrc_prm prm;       /**< Read parameters         */
	struct aubuf *aubuf;        /**< Packet buffer           */
	size_t psize;               /**< Packet size in bytes    */
	size_t sampc;
	uint32_t ptime;
	int16_t *buf;
	int err;

	struct tmr tmr;

	/* Gstreamer */
	char *uri;
	GstElement *pipeline, *bin, *source, *capsfilt, *sink;
};


static struct ausrc *ausrc;

static char *uri_regex = "([a-z][a-z0-9+.-]*):(?://).*";


static GstBusSyncReply
sync_handler(
	GstBus * bus, GstMessage * msg,
	struct ausrc_st *st)
{
	GstTagList *tag_list;
	gchar *title;
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

			warning("gst: Error: %d(%m) message=\"%s\"\n",
				err->code,
				err->code, err->message);
			warning("gst: Debug: %s\n", d);

			g_free(d);

			st->err = err->code;

			/* Call error handler */
			if (st->errh)
				st->errh(err->code, err->message, st->arg);

			g_error_free(err);

			st->run = false;
			break;

		case GST_MESSAGE_TAG:
			gst_message_parse_tag(msg, &tag_list);

			if (gst_tag_list_get_string(
				tag_list,
				GST_TAG_TITLE,
				&title)) {

				info("gst: title: '%s'\n", title);
				g_free(title);
			}
			gst_tag_list_unref(tag_list);
			break;

		default:
			break;
	}

	gst_message_unref(msg);
	return GST_BUS_DROP;
}


static void format_check(struct ausrc_st *st, GstStructure *s)
{
	int rate, channels;

	if (!st || !s)
		return;

	gst_structure_get_int(s, "rate", &rate);
	gst_structure_get_int(s, "channels", &channels);

	if ((int)st->prm.srate != rate) {
		warning("gst: expected %u Hz (got %u Hz)\n", st->prm.srate,
			rate);
	}
	if (st->prm.ch != channels) {
		warning("gst: expected %d channels (got %d)\n",
			st->prm.ch, channels);
	}
}


static void play_packet(struct ausrc_st *st)
{
	struct auframe af;

	auframe_init(&af, AUFMT_S16LE, st->buf, st->sampc, st->prm.srate,
	             st->prm.ch);

	/* timed read from audio-buffer */
	if (st->prm.ptime && aubuf_get_samp(st->aubuf, st->prm.ptime, st->buf,
				st->sampc))
		return;

	/* immediate read from audio-buffer */
	if (!st->prm.ptime)
		aubuf_read_samp(st->aubuf, st->buf, st->sampc);

	/* call read handler */
	if (st->rh)
		st->rh(&af, st->arg);
}


/* Expected format: 16-bit signed PCM */
static void packet_handler(struct ausrc_st *st, GstBuffer *buffer)
{
	GstMapInfo info;
	int err;

	if (!st->run)
		return;

	/* NOTE: When streaming from files, the buffer will be filled up
	 *       pretty quickly..
	 */

	if (!gst_buffer_map(buffer, &info, GST_MAP_READ)) {
		warning("gst: gst_buffer_map failed\n");
		return;
	}

	err = aubuf_write(st->aubuf, info.data, info.size);
	if (err) {
		warning("gst: aubuf_write: %m\n", err);
	}

	gst_buffer_unmap(buffer, &info);

	/* Empty buffer now */
	while (st->run) {
		const struct timespec delay = {0, st->prm.ptime*1000000/2};

		play_packet(st);

		if (aubuf_cur_size(st->aubuf) < st->psize)
			break;

		(void)nanosleep(&delay, NULL);
	}
}


static void handoff_handler(GstElement *sink, GstBuffer *buffer,
			    GstPad *pad, gpointer user_data)
{
	struct ausrc_st *st = user_data;
	GstCaps *caps;
	(void)sink;

	caps = gst_pad_get_current_caps(pad);
	format_check(st, gst_caps_get_structure(caps, 0));
	gst_caps_unref(caps);

	packet_handler(st, buffer);
}


static void set_caps(struct ausrc_st *st)
{
	GstCaps *caps;

	/* Set the capabilities we want */

	caps = gst_caps_new_simple("audio/x-raw",
				   "format", G_TYPE_STRING, "S16LE",
				   "rate",     G_TYPE_INT,    st->prm.srate,
				   "channels", G_TYPE_INT,    st->prm.ch,
				   NULL);

	g_object_set(G_OBJECT(st->capsfilt), "caps", caps, NULL);
	gst_caps_unref(caps);
}


/**
 * Set up the Gstreamer pipeline. The playbin element is used to decode
 * all kinds of different formats. The capsfilter is used to deliver the
 * audio in a fixed format (X Hz, 1-2 channels, 16 bit signed)
 *
 * The pipeline looks like this:
 *
 * <pre>
 *  .--------------.    .------------------------------------------.
 *  |    playbin   |    |mybin    .------------.   .------------.  |
 *  |----.    .----|    |-----.   | capsfilter |   |  fakesink  |  |
 *  |sink|    |src |--->|ghost|   |----.   .---|   |----.   .---|  |    handoff
 *  |----'    '----|    |pad  |-->|sink|   |src|-->|sink|   |src|--+--> handler
 *  |              |    |-----'   '------------'   '------------'  |
 *  '--------------'    '------------------------------------------'
 * </pre>
 *
 * @param st Audio source state
 *
 * @return 0 if success, otherwise errorcode
 */
static int gst_setup(struct ausrc_st *st)
{
	GstBus *bus;
	GstPad *pad;

	st->pipeline = gst_pipeline_new("pipeline");
	if (!st->pipeline) {
		warning("gst: failed to create pipeline element\n");
		return ENOMEM;
	}

	/********************* Player BIN **************************/

	st->source = gst_element_factory_make("playbin", "source");
	if (!st->source) {
		warning("gst: failed to create playbin source element\n");
		return ENOMEM;
	}

	/********************* My BIN **************************/

	st->bin = gst_bin_new("mybin");

	st->capsfilt = gst_element_factory_make("capsfilter", NULL);
	if (!st->capsfilt) {
		warning("gst: failed to create capsfilter element\n");
		return ENOMEM;
	}

	set_caps(st);

	st->sink = gst_element_factory_make("fakesink", "sink");
	if (!st->sink) {
		warning("gst: failed to create sink element\n");
		return ENOMEM;
	}

	g_object_set(st->sink, "async", false, NULL);

	gst_bin_add_many(GST_BIN(st->bin), st->capsfilt, st->sink, NULL);
	gst_element_link_many(st->capsfilt, st->sink, NULL);

	/* add ghostpad */
	pad = gst_element_get_static_pad(st->capsfilt, "sink");
	gst_element_add_pad(st->bin, gst_ghost_pad_new("sink", pad));
	gst_object_unref(GST_OBJECT(pad));

	/* put all elements in a bin */
	gst_bin_add_many(GST_BIN(st->pipeline), st->source, NULL);

	/* Override audio-sink handoff handler */
	g_signal_connect(st->sink, "handoff", G_CALLBACK(handoff_handler), st);
	g_object_set(G_OBJECT(st->sink),
		"signal-handoffs", TRUE,
		"async", FALSE, NULL);

	g_object_set(G_OBJECT(st->source), "audio-sink", st->bin, NULL);

	/********************* Misc **************************/

	/* Bus watch */
	bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));

	gst_bus_enable_sync_message_emission(bus);
	gst_bus_set_sync_handler(
		bus, (GstBusSyncHandler) sync_handler,
		st, NULL);

	gst_object_unref(bus);

	/* Set URI */
	g_object_set(G_OBJECT(st->source), "uri", st->uri, NULL);

	return 0;
}


static int setup_uri(struct ausrc_st *st, const char *device)
{
	int err = 0;

	if (g_regex_match_simple(
		uri_regex, device, 0, G_REGEX_MATCH_NOTEMPTY)) {
		err = str_dup(&st->uri, device);
	}
	else {
		if (!access(device, R_OK)) {
			size_t urilength = strlen(device) + 8;
			char *uri = mem_alloc(urilength, NULL);
			if (re_snprintf(uri, urilength, "file://%s",
					device) < 0)
				return ENOMEM;
			st->uri = uri;
		}
		else {
			err = errno;
		}
	}

	return err;
}


static void gst_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->run) {
		st->run = false;
	}

	tmr_cancel(&st->tmr);

	gst_element_set_state(st->pipeline, GST_STATE_NULL);
	gst_object_unref(GST_OBJECT(st->pipeline));

	mem_deref(st->uri);
	mem_deref(st->aubuf);
	mem_deref(st->buf);
}


static void timeout(void *arg)
{
	struct ausrc_st *st = arg;
	tmr_start(&st->tmr, st->ptime ? st->ptime : 40, timeout, st);

	/* check if source is still running */
	if (!st->run) {
		tmr_cancel(&st->tmr);

		if (st->eos) {
			debug("gst: end of file\n");
			/* error handler must be called from re_main thread */
			if (st->errh)
				st->errh(0, "end of file", st->arg);
		}
	}
}


static int gst_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct ausrc_prm *prm, const char *device,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;

	if (!stp || !as || !prm)
		return EINVAL;

	if (!str_isset(device))
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("gst: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), gst_destructor);
	if (!st)
		return ENOMEM;

	st->rh   = rh;
	st->arg  = arg;

	err = setup_uri(st, device);
	if (err) goto out;

	st->ptime = prm->ptime;
	if (!st->ptime)
		st->ptime = 20;

	if (!prm->srate)
		prm->srate = 16000;

	if (!prm->ch)
		prm->ch = 1;

	st->prm   = *prm;
	st->sampc = prm->srate * prm->ch * st->ptime / 1000;
	st->psize = 2 * st->sampc;

	st->buf = mem_zalloc(st->psize, NULL);
	if (!st->buf) {
		err = ENOMEM;
		goto out;
	}

	err = aubuf_alloc(&st->aubuf, 0, 0);
	if (err)
		goto out;

	err = gst_setup(st);
	if (err)
		goto out;

	st->run = true;
	st->eos = false;

	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);

	if (!st->run) {
		err = st->err;
		goto out;
	}

	st->errh = errh;

	tmr_start(&st->tmr, st->ptime, timeout, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	gst_element_get_state(st->pipeline, NULL, NULL, 500*1000*1000);
	gint64 duration = 0;
	gst_element_query_duration(st->pipeline,
				   GST_FORMAT_TIME,
				   &duration);

	prm->duration = duration / 1000000;
	return err;
}


static int mod_gst_init(void)
{
	gchar *s;

	gst_init(0, NULL);

	s = gst_version_string();

	info("gst: init: %s\n", s);

	g_free(s);

	return ausrc_register(&ausrc, baresip_ausrcl(), "gst", gst_alloc);
}


static int mod_gst_close(void)
{
	ausrc = mem_deref(ausrc);
	gst_deinit();
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(gst) = {
	"gst",
	"sound",
	mod_gst_init,
	mod_gst_close
};
