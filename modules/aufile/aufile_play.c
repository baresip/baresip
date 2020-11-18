/**
 * @file aufile.c WAV Audio Player
 *
 * Copyright (C) 2020 commend.com - Christian Spielberger
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <pthread.h>
#include <string.h>
#include <sndfile.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aufile.h"


struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */
	SNDFILE *sf;
	struct auplay_prm prm;

	pthread_t thread;
	volatile bool run;
	void *sampv;
	size_t sampc;
	size_t num_bytes;
	auplay_write_h *wh;
	void *arg;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;
	/* Wait for termination of other thread */
	if (st->run) {
		debug("aufile: stopping playback thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	sf_close(st->sf);
	mem_deref(st->sampv);
}


static int get_format(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:  return SF_FORMAT_PCM_16;
	case AUFMT_FLOAT:  return SF_FORMAT_FLOAT;
	default:           return 0;
	}
}


static SNDFILE *openfile(const struct auplay_prm *prm, const char *file)
{
	SF_INFO sfinfo;
	SNDFILE *sf;
	int format;

	format = get_format(prm->fmt);
	if (!format) {
		warning("aufile: sample format not supported (%s)\n",
			aufmt_name(prm->fmt));
		return NULL;
	}

	sfinfo.samplerate = prm->srate;
	sfinfo.channels   = prm->ch;
	sfinfo.format     = SF_FORMAT_WAV | format;

	sf = sf_open(file, SFM_WRITE, &sfinfo);
	if (!sf) {
		warning("aufile: could not open: %s\n", file);
		puts(sf_strerror(NULL));
		return NULL;
	}

	return sf;
}


static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;
	uint64_t t;
	int dt;
	uint32_t ptime = st->prm.ptime;

	t = tmr_jiffies();
	st->run = true;
	while (st->run) {
		st->wh(st->sampv, st->sampc, st->arg);

		sf_write_raw(st->sf, st->sampv, st->num_bytes);

		t += ptime;
		dt = t - tmr_jiffies();
		if (dt <= 2)
			continue;

		sys_msleep(dt);
	}

	return NULL;
}


int play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	const char *file;
	int err;

	if (!prm || !wh)
		return EINVAL;

	if (!prm->ch || !prm->srate || !prm->ptime)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	file = "speaker.wav";
	if (str_isset(device))
		file = device;

	st->sf = openfile(prm, file);
	if (!st->sf)
		return EIO;

	st->ap  = ap;
	st->wh = wh;
	st->arg = arg;
	st->prm = *prm;
	st->sampc = st->prm.ch * st->prm.srate * st->prm.ptime / 1000;
	st->num_bytes = st->sampc * aufmt_sample_size(prm->fmt);
	st->sampv = mem_alloc(st->num_bytes, NULL);

	info("aufile: writing speaker audio to %s\n", file);
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

out:
	if (err)
		mem_deref(st);
	else if (stp)
		*stp = st;

	return err;
}
