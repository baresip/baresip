/**
 * @file alsa_src.c  ALSA sound driver - recorder
 *
 * Copyright (C) 2010 Creytiv.com
 */
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


struct ausrc_st {
	struct ausrc *as;      /* inheritance */
	pthread_t thread;
	bool run;
	snd_pcm_t *read;
	struct mbuf *mbr;
	ausrc_read_h *rh;
	void *arg;
	struct ausrc_prm prm;
	char *device;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	if (st->read)
		snd_pcm_close(st->read);

	mem_deref(st->mbr);
	mem_deref(st->as);
	mem_deref(st->device);
}


static void *read_thread(void *arg)
{
	struct ausrc_st *st = arg;
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

	while (st->run) {
		err = snd_pcm_readi(st->read, st->mbr->buf, num_frames);
		if (err == -EPIPE) {
			snd_pcm_prepare(st->read);
			continue;
		}
		else if (err <= 0) {
			continue;
		}

		st->rh(st->mbr->buf, err * 2 * st->prm.ch, st->arg);
	}

 out:
	return NULL;
}


int alsa_src_alloc(struct ausrc_st **stp, struct ausrc *as,
		   struct media_ctx **ctx,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	uint32_t sampc;
	int num_frames;
	int err;
	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;
	if (prm->fmt != AUFMT_S16LE)
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
	st->as  = mem_ref(as);
	st->rh  = rh;
	st->arg = arg;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;
	num_frames = st->prm.srate * st->prm.ptime / 1000;

	st->mbr = mbuf_alloc(2 * sampc);
	if (!st->mbr) {
		err = ENOMEM;
		goto out;
	}

	err = snd_pcm_open(&st->read, st->device, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		warning("alsa: could not open ausrc device '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	err = alsa_reset(st->read, st->prm.srate, st->prm.ch, st->prm.fmt,
			 num_frames);
	if (err) {
		warning("alsa: could not reset source '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
