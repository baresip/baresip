/**
 * @file pastream.c  Pulseaudio sound driver (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com,
 *                                  c.spielberger@commend.com
 *                                  c.huber@commend.com
 */

#include <pulse/pulseaudio.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>

#include "pulse.h"


static int aufmt_to_pulse_format(enum aufmt fmt)
{
	switch (fmt) {
		case AUFMT_S16LE:  return PA_SAMPLE_S16NE;
		case AUFMT_FLOAT:  return PA_SAMPLE_FLOAT32NE;
		default: return 0;
	}
}


static void success_cb(pa_stream *s, int success, void *arg)
{
	(void)s;
	(void)success;
	(void)arg;

	struct paconn_st *c = paconn_get();

	if (c)
		pa_threaded_mainloop_signal(c->mainloop, 0);
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

	while (c && pa_operation_get_state(op) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(c->mainloop);

	pa_operation_unref(op);
	return 0;
}


static void pastream_destructor(void *arg)
{
	struct pastream_st *st = arg;
	struct paconn_st *c = paconn_get();

	if (!c)
		return;

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

	info("pulse: %s [overrun=%zu underrun=%zu]\n",
	     st->sname, st->stats.overrun, st->stats.underrun);
}


static void stream_latency_update_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	pa_usec_t usec;
	int neg;
	int pa_err;
	(void)s;

	pa_err = pa_stream_get_latency(s, &usec, &neg);
	if (!pa_err)
		debug("pulse: stream %s latency update "
				"usec=%lu, neg=%d\n", st->sname, usec, neg);
}


static void stream_underflow_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	(void)s;

	if (!st->shutdown)
		++st->stats.underrun;
}


static void stream_overflow_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	(void)s;

	++st->stats.overrun;
}


static void stream_state_cb(pa_stream *s, void *arg)
{
	struct paconn_st *c = paconn_get();
	(void)s;
	(void)arg;

	if (c)
		pa_threaded_mainloop_signal(c->mainloop, 0);
}


/**
 * Start pulseaudio stream
 *
 * @param st  PA_Stream object
 * @param arg Argument for the read and write handlers
 *
 * @return int 0 if success, errorcode otherwise
 */
int pastream_start(struct pastream_st* st, void *arg)
{
	struct paconn_st *c = paconn_get();
	int pa_err = 0;
	int err = 0;

	if (!c)
		return EINVAL;

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

	pa_stream_set_read_callback(st->stream, stream_read_cb, arg);
	pa_stream_set_write_callback(st->stream, stream_write_cb, arg);
	pa_stream_set_latency_update_callback(st->stream,
					      stream_latency_update_cb, st);
	pa_stream_set_underflow_callback(st->stream,
					 stream_underflow_cb, st);
	pa_stream_set_overflow_callback(st->stream,
					stream_overflow_cb, st);
	pa_stream_set_state_callback(st->stream, stream_state_cb, st);

	const char *device = NULL;
	if (str_len(st->device) && str_casecmp(st->device, "default") != 0) {
		device = st->device;
	}

	if (st->direction == PA_STREAM_PLAYBACK) {
		pa_err = pa_stream_connect_playback(st->stream,
				device, &st->attr,
				PA_STREAM_INTERPOLATE_TIMING |
				PA_STREAM_ADJUST_LATENCY |
				PA_STREAM_AUTO_TIMING_UPDATE,
				NULL, NULL);
	}
	else if (st->direction == PA_STREAM_RECORD) {
		pa_err = pa_stream_connect_record(st->stream,
				device, &st->attr,
				PA_STREAM_INTERPOLATE_TIMING |
				PA_STREAM_ADJUST_LATENCY |
				PA_STREAM_AUTO_TIMING_UPDATE);
	}
	else {
		warning("pulse: stream %s unsupported stream "
			"direction %d\n",
			st->sname, (int)st->direction);
	}

out:
	if (st && pa_err) {
		warning("pulse: stream %s stream error %d\n", st->sname,
			pa_err);
		err = EINVAL;
	}

	pa_threaded_mainloop_unlock(c->mainloop);
	return err;
}


/**
 * Allocate internal PA_STREAM object
 *
 * @param bptr  Base pointer to a pastream_st object
 * @param dev   Device name
 * @param pname Program name
 * @param sname Stream name
 * @param dir   Stream direction
 * @param srate Stream sample rate
 * @param ch    Stream channel number
 * @param ptime Stream ptime
 * @param fmt   Stream format
 *
 * @return int 0 if success, errorcode otherwise
 */
int pastream_alloc(struct pastream_st **bptr, const char *dev,
	const char *pname, const char *sname, pa_stream_direction_t dir,
	uint32_t srate, uint8_t ch, uint32_t ptime, int fmt)
{
	struct pastream_st *st;

	if (!bptr)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), pastream_destructor);
	if (!st)
		return ENOMEM;

	st->ss.format   = aufmt_to_pulse_format(fmt);
	st->ss.channels = ch;
	st->ss.rate     = srate;

	st->attr.maxlength = UINT32_MAX;
	st->attr.tlength   = (uint32_t) pa_usec_to_bytes(
			     ptime * PA_USEC_PER_MSEC, &st->ss);
	st->attr.prebuf    = UINT32_MAX;
	st->attr.minreq    = st->attr.tlength / 4;
	st->attr.fragsize  = (uint32_t) pa_usec_to_bytes(
			     ptime / 3 * PA_USEC_PER_MSEC, &st->ss);

	st->direction = dir;

	str_ncpy(st->pname, pname, sizeof(st->pname));
	str_ncpy(st->sname, sname, sizeof(st->sname));
	str_ncpy(st->device, dev, sizeof(st->device));

	*bptr = st;
	return 0;
}
