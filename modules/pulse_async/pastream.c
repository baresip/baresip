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
#include <stdint.h>

#include "pulse.h"


#define DEBUG_MODULE "pulse_async/pastream"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


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


static void pastream_destructor(void *arg)
{
	struct pastream_st *st = arg;
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

	if (st->sampv) {
		st->sampv = mem_deref(st->sampv);
	}

	pa_threaded_mainloop_unlock(c->mainloop);
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
		debug("pulse_async: stream %s latency update "
				"usec=%lu, neg=%d\n", st->sname, usec, neg);
}


static void stream_underflow_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	(void)s;

	if (!st->shutdown)
		warning("pulse_async: stream %s underrun\n",  st->sname);
}


static void stream_overflow_cb(pa_stream *s, void *arg)
{
	struct pastream_st *st = arg;
	(void)s;

	warning("pulse_async: stream %s overrun\n", st->sname);
}


static void stream_state_cb(pa_stream *s, void *arg)
{
	struct paconn_st *c = paconn_get();
	(void)s;
	(void)arg;

	debug("pulse_async: stream state %d\n", pa_stream_get_state(s));
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

	pa_stream_set_read_callback(st->stream, stream_read_cb, st);
	pa_stream_set_write_callback(st->stream, stream_write_cb, st);
	pa_stream_set_latency_update_callback(st->stream,
					      stream_latency_update_cb, st);
	pa_stream_set_underflow_callback(st->stream,
					 stream_underflow_cb, st);
	pa_stream_set_overflow_callback(st->stream,
					stream_overflow_cb, st);
	pa_stream_set_state_callback(st->stream, stream_state_cb, st);

	if (st->direction == PA_STREAM_PLAYBACK) {
		DEBUG_INFO("Connect to stream \n");
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


static int aufmt_to_pulse_format(enum aufmt fmt)
{
	switch (fmt) {
		case AUFMT_S16LE:  return PA_SAMPLE_S16NE;
		case AUFMT_FLOAT:  return PA_SAMPLE_FLOAT32NE;
		default: return 0;
	}
}


int pastream_alloc(struct pastream_st **bptr, struct auplay_prm *prm,
	const char *dev, const char *pname, const char *sname,
	pa_stream_direction_t dir, void *arg)
{
	struct pastream_st *st;

	if (!bptr || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), pastream_destructor);
	if (!st)
		return ENOMEM;

	st->arg = arg;
	st->play_prm = *prm;
	st->sampsz = aufmt_sample_size(prm->fmt);
	st->ss.format = aufmt_to_pulse_format(prm->fmt);
	st->ss.channels = prm->ch;
	st->ss.rate = prm->srate;
	st->sz = prm->ptime * prm->ch * st->sampsz * prm->srate / 1000;


	st->attr.maxlength = UINT32_MAX;
	st->attr.tlength = (uint32_t)pa_usec_to_bytes(
		prm->ptime * PA_USEC_PER_MSEC, &st->ss);

	st->attr.prebuf = UINT32_MAX;
	st->attr.minreq = st->attr.tlength / 4;

	st->attr.fragsize = (uint32_t)pa_usec_to_bytes(
		prm->ptime / 3 * PA_USEC_PER_MSEC, &st->ss);
	st->direction = dir;

	if (st->direction == PA_STREAM_RECORD) {
		st->sampc = prm->ptime * prm->ch * prm->srate / 1000;
		st->sampv = mem_zalloc(st->sampsz * st->sampc, NULL);
		if (!st->sampv) {
			mem_deref(st);
			return ENOMEM;
		}
	}

	strcpy(st->pname, pname);
	strcpy(st->sname, sname);
	str_ncpy(st->device, dev, sizeof(st->device));

	*bptr = st;

	return 0;
}


void pastream_set_writehandler(struct pastream_st *st, auplay_write_h *wh)
{
	st->wh = wh;
}


void pastream_set_readhandler(struct pastream_st *st, ausrc_read_h *rh)
{
	st->rh = rh;
}