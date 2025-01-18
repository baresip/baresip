/**
 * @file aufile_play.c WAV Audio Player
 *
 * Copyright (C) 2020 commend.com - Christian Spielberger
 */
#include <string.h>
#include <re_atomic.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aufile.h"


struct auplay_st {
	struct aufile *auf;
	struct auplay_prm prm;

	thrd_t thread;
	RE_ATOMIC bool run;
	void *sampv;
	size_t sampc;
	size_t num_bytes;
	auplay_write_h *wh;
	void *arg;
};


static void destructor(void *arg)
{
	struct auplay_st *st = arg;
	/* Wait for termination of other thread */
	if (re_atomic_rlx(&st->run)) {
		debug("aufile: stopping playback thread\n");
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	mem_deref(st->auf);
	mem_deref(st->sampv);
}


static int write_thread(void *arg)
{
	struct auplay_st *st = arg;
	uint64_t t;
	int dt;
	int err;
	uint32_t ptime = st->prm.ptime;

	t = tmr_jiffies();
	while (re_atomic_rlx(&st->run)) {
		struct auframe af;

		auframe_init(&af, st->prm.fmt, st->sampv, st->sampc,
			     st->prm.srate, st->prm.ch);

		af.timestamp = t * 1000;

		st->wh(&af, st->arg);

		err = aufile_write(st->auf, st->sampv, st->num_bytes);
		if (err)
			break;

		t += ptime;
		dt = (int)(t - tmr_jiffies());
		if (dt <= 2)
			continue;

		sys_msleep(dt);
	}

	return 0;
}


int aufile_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	const char *file;
	struct aufile_prm aufprm;
	int err;
	(void)ap;

	if (!prm || !wh)
		return EINVAL;

	if (!prm->ch || !prm->srate || !prm->ptime)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	file = "speaker.wav";
	if (str_isset(device))
		file = device;

	aufprm.srate    = prm->srate;
	aufprm.channels = prm->ch;
	aufprm.fmt      = prm->fmt;
	err = aufile_open(&st->auf, &aufprm, file, AUFILE_WRITE);
	if (err) {
		warning("aufile: could not open %s for writing\n", file);
		return err;
	}

	st->wh = wh;
	st->arg = arg;
	st->prm = *prm;
	st->sampc = st->prm.ch * st->prm.srate * st->prm.ptime / 1000;
	st->num_bytes = st->sampc * aufmt_sample_size(prm->fmt);
	st->sampv = mem_zalloc(st->num_bytes, NULL);
	if (!st->sampv)
		return ENOMEM;

	info("aufile: writing speaker audio to %s\n", file);
	re_atomic_rlx_set(&st->run, true);
	err = thread_create_name(&st->thread, "aufile_play", write_thread, st);
	if (err) {
		re_atomic_rlx_set(&st->run, false);
		goto out;
	}

out:
	if (err)
		mem_deref(st);
	else if (stp)
		*stp = st;

	return err;
}
