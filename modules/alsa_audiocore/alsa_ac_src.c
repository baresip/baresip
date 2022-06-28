/**
 * @file alsa_ac_src.c  ALSA sound driver - recorder
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


static void ausrc_destructor(void *arg)
{
	struct alsa_src_st *st = arg;

	if (st->read)
		snd_pcm_close(st->read);

	mem_deref(st->device);
}


void alsa_ac_src_read_frames(struct alsa_src_st *st, void* sampv,
			  size_t num_frames)
{
	snd_pcm_sframes_t n;

	n = snd_pcm_readi(st->read, sampv, num_frames);
	if (n == -EPIPE) {
		warning("alsa_audiocore: read overrun\n");
		snd_pcm_prepare(st->read);
		n = snd_pcm_readi(st->read, sampv, num_frames);
	}
	if ((size_t) n < num_frames)
		warning("alsa_audiocore: read %i of %i\n",
				(int)n, (int)num_frames);
}

void alsa_ac_src_read(struct alsa_src_st *st, void* sampv)
{
	alsa_ac_src_read_frames(st, sampv, st->num_frames);
}

int alsa_ac_src_alloc(struct alsa_src_st **stp,
		   struct ausrc_prm *prm, const char *device)
{
	struct alsa_src_st *st;
	snd_pcm_format_t pcmfmt;
	int err;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	err = str_dup(&st->device, device);
	if (err)
		goto out;

	st->prm = *prm;

	st->num_frames = prm->srate * prm->ptime / 1000;
	st->sampc = st->num_frames * prm->ch;

	err = snd_pcm_open(&st->read, st->device, SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0) {
		warning("alsa_audiocore: could not open ausrc "
				"device '%s' (%s)\n",
				st->device, snd_strerror(err));
		goto out;
	}

	pcmfmt = alsa_ac_aufmt_to_alsaformat(prm->fmt);
	if (pcmfmt == SND_PCM_FORMAT_UNKNOWN) {
		warning("alsa_audiocore: unknown sample format '%s'\n",
			aufmt_name(prm->fmt));
		err = EINVAL;
		goto out;
	}

	err = alsa_ac_reset(st->read, st->prm.srate, st->prm.ch,
			st->num_frames, pcmfmt);
	if (err) {
		warning("alsa_audiocore: could not reset source '%s' (%s)\n",
			st->device, snd_strerror(err));
		goto out;
	}

	debug("alsa_audiocore: recording started (%s) format=%s\n",
		st->device, aufmt_name(prm->fmt));

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
