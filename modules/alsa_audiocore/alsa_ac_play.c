/**
 * @file alsa_ac_play.c  ALSA sound driver - player
 *
 * Copyright (C) 2022 Commend.com - h.ramoser@commend.com
 */
#define _DEFAULT_SOURCE 1
#define _POSIX_SOURCE 1
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "alsa_audiocore.h"


static void auplay_destructor(void *arg)
{
	struct alsa_play_st *st = arg;

	if (st->write) {
		snd_pcm_drop(st->write);
		snd_pcm_close(st->write);
	}

	mem_deref(st->sampv);
	mem_deref(st->device);
}

void alsa_ac_play_write_frames(struct alsa_play_st *st, void *sampv,
			  size_t num_frames)
{
	snd_pcm_sframes_t n;

	if (!sampv)
		sampv = st->sampv; /* write zeros */

	n = snd_pcm_writei(st->write, sampv, num_frames);

	if (-EPIPE == n) {
		snd_pcm_prepare(st->write);

		n = snd_pcm_writei(st->write, sampv, num_frames);
		warning("alsa_audiocore: write underrun\n");
	}
	if (n < 0) {
		warning("alsa_audiocore: write error: %s\n",
			snd_strerror((int) n));
	}
	else if ((size_t) n != num_frames) {
		warning("alsa_audiocore: write: wrote %d of %d samples\n",
			(int) n, num_frames);
	}
}


void alsa_ac_play_write(struct alsa_play_st *st, void *sampv)
{
	alsa_ac_play_write_frames(st, sampv, st->num_frames);
}

int alsa_ac_play_alloc(struct alsa_play_st **stp,
		    struct auplay_prm *prm, const char *device)
{
	struct alsa_play_st *st;
	snd_pcm_format_t pcmfmt;
	int err;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	err = str_dup(&st->device, device);
	if (err)
		goto out;

	st->prm = *prm;

	st->num_frames = prm->srate * prm->ptime / 1000;
	st->sampc = st->num_frames * prm->ch;

	st->sampv = mem_zalloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	err = snd_pcm_open(&st->write, st->device, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		warning("alsa_audiocore: could not open auplay "
				"device '%s' (%s)\n",
				st->device, snd_strerror(err));
		info("consider using dmix as your default alsa device\n");
		goto out;
	}

	pcmfmt = alsa_ac_aufmt_to_alsaformat(prm->fmt);
	if (pcmfmt == SND_PCM_FORMAT_UNKNOWN) {
		warning("alsa_audiocore: unknown sample format '%s'\n",
			aufmt_name(prm->fmt));
		err = EINVAL;
		goto out;
	}

	err = alsa_ac_reset(st->write, st->prm.srate, st->prm.ch,
			st->num_frames, pcmfmt);
	if (err) {
		warning("alsa_audiocore: could not reset player '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	debug("alsa_audiocore: playback started (%s)\n", st->device);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
