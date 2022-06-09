/**
 * @file recorder.c  Pulseaudio sound driver - recorder (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com,
 *                                  c.spielberger@commend.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>
#include <pulse/pulseaudio.h>
#include "pulse.h"

#define DEBUG_MODULE "pulse_async/recorder"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


struct ausrc_st {
	struct pastream_st *b;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	mem_deref(st->b);
}


int pulse_async_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
	struct ausrc_prm *prm, const char *dev, ausrc_read_h *rh,
	ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err = 0;
	(void) errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	info ("pulse_async: opening recorder(%u Hz, %d channels,"
		"device '%s')\n", prm->srate, prm->ch, dev);

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	err = pastream_alloc(&st->b, (struct auplay_prm *)prm,
		dev, "Baresip", "VoIP Recorder",
		PA_STREAM_RECORD, arg);
	if (err)
		goto out;

	pastream_set_readhandler(st->b, rh);
	err = pastream_start(st->b);
	if (err) {
		warning("pulse_async: could not connect record stream %s "
			"(%m)\n", st->b->sname, err);
		err = ENODEV;
		goto out;
	}

	info ("pulse_async: record stream %s started\n", st->b->sname);

  out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static void dev_list_cb(pa_context *context, const pa_source_info *l, int eol,
			void *arg)
{
	struct list *dev_list = arg;
	int err;
	(void)context;

	if (eol > 0)
		return;

	if (strstr(l->name, "output"))
		return;

	err = mediadev_add(dev_list, l->name);
	if (err)
		warning("pulse_async: record device %s could not be added\n",
			l->name);
}


static pa_operation *get_dev_info(pa_context *context, struct list *dev_list)
{
	return pa_context_get_source_info_list(context, dev_list_cb, dev_list);
}


int pulse_async_recorder_init(struct ausrc *as)
{
	if (!as)
		return EINVAL;

	list_init(&as->dev_list);
	return pulse_async_set_available_devices(&as->dev_list, get_dev_info);
}


void stream_read_cb(pa_stream *s, size_t len, void *arg)
{
	struct pastream_st *st = arg;
	struct auframe af;

	int pa_err = 0;
	size_t sampc = 0;
	size_t idx = 0;
	const void *pabuf = NULL;
	size_t rlen = 0;

	(void) len;

	if (st->shutdown)
		return;

	while (pa_stream_readable_size(s) > 0) {
		pa_err = pa_stream_peek(s, &pabuf, &rlen);
		if (pa_err < 0) {
			warning ("pulse_async: %s pa_stream_peek error (%s)\n",
				st->sname, pa_strerror(pa_err));
			return;
		}

		if (!rlen)
			return;

		sampc += rlen / st->sampsz;
		if (sampc > st->sampc) {
			st->sampv = mem_realloc(st->sampv, st->sampsz * sampc);
			st->sampc = sampc;
		}


		if (!st->sampv) {
			pa_stream_drop(s);
			continue;
		}

		if (pabuf)
			memcpy((uint8_t *) st->sampv + idx, pabuf, rlen);
		else
			memset((uint8_t *) st->sampv + idx, 0, rlen);

		idx += rlen;
		pa_stream_drop(s);

	}

	auframe_init(&af, st->play_prm.fmt, st->sampv, sampc,
		st->play_prm.srate, st->play_prm.ch);

	af.timestamp = st->samps * AUDIO_TIMEBASE
		/ (st->play_prm.srate * st->play_prm.ch);
	st->samps += sampc;
	st->rh(&af, st->arg);
}