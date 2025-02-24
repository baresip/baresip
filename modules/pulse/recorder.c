/**
 * @file recorder.c  Pulseaudio sound driver - recorder (asynchronous API)
 *
 * Copyright (C) 2021 Commend.com - h.ramoser@commend.com
 *                                  c.spielberger@commend.com
 *                                  c.huber@commend.com
 */

#include <pulse/pulseaudio.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>

#include "pulse.h"


struct ausrc_st {
	struct pastream_st *b;

	struct ausrc_prm src_prm;
	ausrc_read_h *rh;
	ausrc_error_h *errh;

	void   *sampv;
	size_t  sampsz;
	size_t  sampc;
	uint64_t samps;

	void *arg;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	mem_deref(st->b);
	mem_deref(st->sampv);
	st->rh = NULL;
	st->errh = NULL;
}


int pulse_recorder_alloc(struct ausrc_st **stp, const struct ausrc *as,
	struct ausrc_prm *prm, const char *dev, ausrc_read_h *rh,
	ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err = 0;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	info ("pulse: opening recorder(%u Hz, %d channels,"
	      "device '%s')\n", prm->srate, prm->ch, dev);

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->src_prm.srate = prm->srate;
	st->src_prm.ch    = prm->ch;
	st->src_prm.ptime = prm->ptime;
	st->src_prm.fmt   = prm->fmt;

	st->sampsz = aufmt_sample_size(prm->fmt);
	st->sampc  = prm->ptime * prm->ch * prm->srate / 1000;
	st->samps  = 0;
	st->sampv  = mem_zalloc(st->sampsz * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	st->rh   = rh;
	st->errh = errh;
	st->arg  = arg;

	err = pastream_alloc(&st->b, dev, "Baresip", "VoIP Recorder",
		PA_STREAM_RECORD, prm->srate, prm->ch, prm->ptime, prm->fmt);
	if (err)
		goto out;

	err = pastream_start(st->b, st);
	if (err) {
		warning("pulse: could not connect record stream %s "
			"(%m)\n", st->b->sname, err);
		err = ENODEV;
		goto out;
	}

	info ("pulse: record stream %s started\n", st->b->sname);

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
		warning("pulse: record device %s could not be added\n",
			l->name);
}


static pa_operation *get_dev_info(pa_context *context, struct list *dev_list)
{
	return pa_context_get_source_info_list(context, dev_list_cb, dev_list);
}


int pulse_recorder_init(struct ausrc *as)
{
	if (!as)
		return EINVAL;

	list_init(&as->dev_list);
	return pulse_set_available_devices(&as->dev_list, get_dev_info);
}


/**
 * Source read callback function which gets called by pulseaudio
 *
 * @param s   Pulseaudio stream object
 * @param len Number of bytes to read (not used)
 * @param arg Argument (ausrc_st object)
 */
void stream_read_cb(pa_stream *s, size_t len, void *arg)
{
	struct ausrc_st *st = arg;
	struct paconn_st *c = paconn_get();
	struct auframe af;

	int pa_err = 0;
	size_t sampc = 0;
	size_t idx = 0;
	const void *pabuf = NULL;
	size_t rlen = 0;

	(void) len;

	if (st->b->shutdown)
		goto out;

	while (pa_stream_readable_size(s) > 0) {
		pa_err = pa_stream_peek(s, &pabuf, &rlen);
		if (pa_err < 0) {
			warning ("pulse: %s pa_stream_peek error (%s)\n",
				st->b->sname, pa_strerror(pa_err));
			goto out;
		}

		if (!rlen)
			goto out;

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

	auframe_init(&af, st->src_prm.fmt, st->sampv, sampc,
		st->src_prm.srate, st->src_prm.ch);

	af.timestamp = st->samps * AUDIO_TIMEBASE /
		       (st->src_prm.srate * st->src_prm.ch);
	st->samps += sampc;
	st->rh(&af, st->arg);

out:
	if (c)
		pa_threaded_mainloop_signal(c->mainloop, 0);
}
