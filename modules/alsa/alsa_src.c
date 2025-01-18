/**
 * @file alsa_src.c  ALSA sound driver - recorder
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <re_atomic.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "alsa.h"


struct ausrc_st {
	thrd_t thread;
	RE_ATOMIC bool run;
	snd_pcm_t *read;
	void *sampv;
	size_t sampc;
	ausrc_read_h *rh;
	void *arg;
	struct ausrc_prm prm;
	char *device;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	/* Wait for termination of other thread */
	if (re_atomic_rlx(&st->run)) {
		debug("alsa: stopping recording thread (%s)\n", st->device);
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	if (st->read)
		snd_pcm_close(st->read);

	mem_deref(st->sampv);
	mem_deref(st->device);
}


static int read_thread(void *arg)
{
	struct ausrc_st *st = arg;
	uint64_t frames = 0;
	int num_frames;
	int err;

	num_frames = st->prm.srate * st->prm.ptime / 1000;

	/* Start */
	err = snd_pcm_start(st->read);
	if (err) {
		warning("alsa: could not start ausrc device '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	while (re_atomic_rlx(&st->run)) {
		struct auframe af;
		long n;

		n = snd_pcm_readi(st->read, st->sampv, num_frames);
		if (n == -EPIPE) {
			snd_pcm_prepare(st->read);
			continue;
		}
		else if (n <= 0) {
			continue;
		}

		auframe_init(&af, st->prm.fmt, st->sampv, n * st->prm.ch,
			     st->prm.srate, st->prm.ch);
		af.timestamp = frames * AUDIO_TIMEBASE / st->prm.srate;

		frames += n;

		st->rh(&af, st->arg);
	}

 out:
	return err;
}


int alsa_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	snd_pcm_format_t pcmfmt;
	int num_frames;
	int err;
	(void)errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	if (!str_isset(device))
		device = alsa_dev;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	err = str_dup(&st->device, device);
	if (err)
		goto out;

	st->prm = *prm;
	st->rh  = rh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	num_frames = st->prm.srate * st->prm.ptime / 1000;

	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	err = snd_pcm_open(&st->read, st->device, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		warning("alsa: could not open ausrc device '%s' (%s)\n",
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

	err = alsa_reset(st->read, st->prm.srate, st->prm.ch, num_frames,
			 pcmfmt);
	if (err) {
		warning("alsa: could not reset source '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	re_atomic_rlx_set(&st->run, true);
	err = thread_create_name(&st->thread, "alsa_src", read_thread, st);
	if (err) {
		re_atomic_rlx_set(&st->run, false);
		goto out;
	}

	debug("alsa: recording started (%s) format=%s\n",
	      st->device, aufmt_name(prm->fmt));

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
