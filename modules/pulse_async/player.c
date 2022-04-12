/**
 * @file player.c  Pulseaudio sound driver - player (asynchronous API)
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

#define DEBUG_MODULE "pulse_async/player"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


struct auplay_st {
	struct pastream_st *b;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	mem_deref(st->b);
}


int pulse_async_player_alloc(struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *dev, auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err = 0;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	info ("pulse_async: opening player (%u Hz, %d channels, device %s, "
		"ptime %u)\n", prm->srate, prm->ch, dev, prm->ptime);

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	err = pastream_alloc(&st->b, prm, dev, "Baresip", "VoIP Player",
		PA_STREAM_PLAYBACK, arg);
	if (err)
		goto out;


	pastream_set_writehandler(st->b, wh);
	err = pastream_start(st->b);
	if (err) {
		warning("pulse_async: could not connect playback stream %s "
			"(%m)\n", st->b->sname, err);
		err = ENODEV;
		goto out;
	}

	info ("pulse_async: playback stream %s started\n", st->b->sname);

  out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static void dev_list_cb(pa_context *context, const pa_sink_info *l, int eol,
			void *arg)
{
	struct list *dev_list = arg;
	int err;
	(void)context;

	if (eol > 0)
		return;

	err = mediadev_add(dev_list, l->name);
	if (err)
		warning("pulse_async: playback device %s could not be added\n",
			l->name);
}


static pa_operation *get_dev_info(pa_context *context, struct list *dev_list)
{
	return pa_context_get_sink_info_list(context, dev_list_cb, dev_list);
}


int pulse_async_player_init(struct auplay *ap)
{
	if (!ap)
		return EINVAL;

	list_init(&ap->dev_list);
	return pulse_async_set_available_devices(&ap->dev_list, get_dev_info);
}


void stream_write_cb(pa_stream *s, size_t len, void *arg)
{
	struct pastream_st *st = arg;
	struct paconn_st *c = paconn_get();
	struct auframe af;
	void *sampv;
	int pa_err = 0;
	size_t sz = len;
	static uint64_t t0 = 0;
	uint64_t t;

	if (st->shutdown)
		goto out;

	t = tmr_jiffies_usec();
	if (!t0)
		t0 = t;

	pa_err = pa_stream_begin_write(s, &sampv, &sz);
	if (pa_err || !sampv) {
		warning("pulse_async: pa_stream_begin_write error (%s)\n",
			pa_strerror(pa_err));
		goto out;
	}

	auframe_init(&af, st->play_prm.fmt, sampv,
		     sz / st->sampsz,
		     st->play_prm.srate, st->play_prm.ch);

	st->wh(&af, st->arg);

	pa_err= pa_stream_write(s, sampv, sz, NULL, 0LL, PA_SEEK_RELATIVE);
	if (pa_err < 0) {
		warning("pulse_async: pa_stream_write error (%s)\n",
			pa_strerror(pa_err));
	}

out:
	pa_threaded_mainloop_signal(c->mainloop, 0);
}
