/**
 * @file alsa_play.c  ALSA sound driver - player
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _POSIX_SOURCE 1
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "alsa.h"


struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */
	pthread_t thread;
	bool run;
	snd_pcm_t *write;
	void *sampv;
	size_t sampc;
	auplay_write_h *wh;
	void *arg;
	struct auplay_prm prm;
	char *device;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		debug("alsa: stopping playback thread (%s)\n", st->device);
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	if (st->write)
		snd_pcm_close(st->write);

	mem_deref(st->sampv);
	mem_deref(st->device);
}


static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;
	int n;
	int num_frames;

	num_frames = st->prm.srate * st->prm.ptime / 1000;

	while (st->run) {
		const int samples = num_frames;
		void *sampv;

		st->wh(st->sampv, st->sampc, st->arg);

		sampv = st->sampv;

		n = snd_pcm_writei(st->write, sampv, samples);

		if (-EPIPE == n) {
			snd_pcm_prepare(st->write);

			n = snd_pcm_writei(st->write, sampv, samples);
			if (n != samples) {
				warning("alsa: write error: %s\n",
					snd_strerror(n));
			}
		}
		else if (n < 0) {
			warning("alsa: write error: %s\n", snd_strerror(n));
		}
		else if (n != samples) {
			warning("alsa: write: wrote %d of %d samples\n",
				n, samples);
		}
	}

	snd_pcm_drain(st->write);

	return NULL;
}


int alsa_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	snd_pcm_format_t pcmfmt;
	int num_frames;
	int err;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	if (!str_isset(device))
		device = alsa_dev;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	err = str_dup(&st->device, device);
	if (err)
		goto out;

	st->prm = *prm;
	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	num_frames = st->prm.srate * st->prm.ptime / 1000;

	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	err = snd_pcm_open(&st->write, st->device, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		warning("alsa: could not open auplay device '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	pcmfmt = aufmt_to_alsaformat(prm->fmt);
	if (pcmfmt == SND_PCM_FORMAT_UNKNOWN) {
		warning("alsa: unknown sample format '%s'\n",
			aufmt_name(prm->fmt));
		err = EINVAL;
		goto out;
	}

	err = alsa_reset(st->write, st->prm.srate, st->prm.ch, num_frames,
			 pcmfmt);
	if (err) {
		warning("alsa: could not reset player '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	st->run = true;
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("alsa: playback started (%s)\n", st->device);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
