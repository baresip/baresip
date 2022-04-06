/**
 * @file pastream.c  Pulseaudio sound driver (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com,
 *                                  c.spielberger@commend.com
 */
#include <pulse/pulseaudio.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>


#include "pulse.h"


static void success_cb(pa_stream *s, int success, void *arg)
{
	(void)s;
	(void)success;
	(void)arg;

	pa_threaded_mainloop_signal(paconn_get()->mainloop, 0);
}


static int stream_flush(struct pastream_st *st)
{
	struct paconn_st *c = paconn_get();
	pa_operation *op;

	if (!st->stream)
		return EINVAL;

	if (pa_stream_get_state(st->stream) != PA_STREAM_READY)
		return 0;

	op = pa_stream_flush(st->stream, success_cb, st);
	if (!op)
		return EINVAL;

	while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(c->mainloop);

	pa_operation_unref(op);
	return 0;
}


void pastream_cleanup(struct pastream_st *st)
{
	struct paconn_st *c = paconn_get();

	pa_threaded_mainloop_lock(c->mainloop);
	st->shutdown = true;
	if (st->stream) {
		pa_stream_set_write_callback(st->stream, NULL, NULL);
		pa_stream_set_read_callback(st->stream, NULL, NULL);
		pa_stream_set_underflow_callback(st->stream, NULL, NULL);
		pa_stream_set_overflow_callback(st->stream, NULL, NULL);
		pa_stream_set_latency_update_callback(st->stream, NULL, NULL);
		if (st->direction == PA_STREAM_PLAYBACK)
			stream_flush(st);

		pa_stream_disconnect(st->stream);
		pa_stream_unref(st->stream);
		st->stream = NULL;
	}

	pa_threaded_mainloop_unlock(c->mainloop);
}


static void stream_latency_update_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	struct paconn_st *c = paconn_get();
	pa_usec_t usec;
	int neg;
	int pa_err;
	(void)s;

	pa_err = pa_stream_get_latency(s, &usec, &neg);
	if (!pa_err)
		debug("pulse_async: stream %s latency update "
				"usec=%lu, neg=%d\n", st->sname, usec, neg);

	pa_threaded_mainloop_signal(c->mainloop, 0);
}


static void stream_underflow_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	struct paconn_st *c = paconn_get();
	(void)s;

	if (!st->shutdown)
		warning("pulse_async: stream %s underrun\n",  st->sname);

	pa_threaded_mainloop_signal(c->mainloop, 0);
}


static void stream_overflow_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	struct paconn_st *c = paconn_get();
	(void)s;

	warning("pulse_async: stream %s overrun\n", st->sname);
	pa_threaded_mainloop_signal(c->mainloop, 0);
}


int pastream_start(struct pastream_st* st)
{
	struct paconn_st *c = paconn_get();
	int pa_err = 0;
	int err = 0;

	pa_threaded_mainloop_lock(c->mainloop);
	if (!c->context ||
	    pa_context_get_state(c->context) != PA_CONTEXT_READY) {
		err = EINVAL;
		goto out;
	}

	if (st->stream)
		goto out;

	st->stream = pa_stream_new(c->context, st->sname, &st->ss, NULL);
	if (!st->stream) {
		pa_err = pa_context_errno(c->context);
		goto out;
	}

	// pa_stream_set_read_callback(st->stream, stream_read_cb, st);
	// pa_stream_set_write_callback(st->stream, stream_write_cb, st);
	pa_stream_set_latency_update_callback(st->stream,
					      stream_latency_update_cb, st);
	pa_stream_set_underflow_callback(st->stream,
					 stream_underflow_cb, st);
	pa_stream_set_overflow_callback(st->stream,
					stream_overflow_cb, st);

	if (st->direction == PA_STREAM_PLAYBACK) {
		pa_err = pa_stream_connect_playback(st->stream,
				strlen(st->device) == 0 ? NULL :
				st->device, &st->attr,
				PA_STREAM_INTERPOLATE_TIMING |
				PA_STREAM_ADJUST_LATENCY |
				PA_STREAM_AUTO_TIMING_UPDATE,
				NULL, NULL);
	}
	else if (st->direction == PA_STREAM_RECORD) {
		pa_err = pa_stream_connect_record(st->stream,
				strlen(st->device) == 0 ? NULL :
				st->device, &st->attr,
				PA_STREAM_INTERPOLATE_TIMING |
				PA_STREAM_ADJUST_LATENCY |
				PA_STREAM_AUTO_TIMING_UPDATE);
	}
	else {
		warning("pulse_async: stream %s unsupported stream "
			"direction %d\n",
			st->sname, (int)st->direction);
	}

out:
	if (st && pa_err) {
		warning("pulse_async: stream %s stream error %d\n", st->sname,
			pa_err);
		err = EINVAL;
	}

	pa_threaded_mainloop_unlock(c->mainloop);
	return err;
}


int aufmt_to_pulse_format(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return PA_SAMPLE_S16NE;
	case AUFMT_FLOAT:  return PA_SAMPLE_FLOAT32NE;
	default: return 0;
	}
}