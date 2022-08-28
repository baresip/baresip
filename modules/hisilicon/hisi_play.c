/**
 * @file alsa_play.c  ALSA sound driver - player
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _POSIX_SOURCE 1
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
//#include <alsa/asoundlib.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "hisi.h"
#include "errors.h"

#include <mpi_audio.h>
#include <mpi_sys.h>

struct auplay_st {
	pthread_t thread;
	volatile bool run;
	//snd_pcm_t *write;
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
		debug("hisi: stopping playback thread (%s)\n", st->device);
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	int ret = HI_MPI_AO_DisableChn(0, 0);
	if (HI_SUCCESS != ret) {
		printf("error %s\n", hi_errstr(ret));
	}

	ret = HI_MPI_AO_Disable(0);
	if (HI_SUCCESS != ret) {
		printf("error %s\n", hi_errstr(ret));
	}

	mem_deref(st->sampv);
	mem_deref(st->device);
}


static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;
	struct auframe af;
	//snd_pcm_sframes_t n;

	auframe_init(&af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate,
		     st->prm.ch);

	while (st->run) {
		st->wh(&af, st->arg);


		//printf("[%d]\n", st->sampc);
      AUDIO_FRAME_S stData = {
	      .enBitwidth = AUDIO_BIT_WIDTH_16,
	      .enSoundmode = AUDIO_SOUND_MODE_MONO,
	      .u32Len = st->sampc * 2,
	      .u64VirAddr[0] = st->sampv,
      };
      int ret = HI_MPI_AO_SendFrame(0, 0, &stData, -1);
      if (ret != HI_SUCCESS) {
        printf("error %s\n", hi_errstr(ret));
      }

#if 0
		n = snd_pcm_writei(st->write, sampv, samples);

		if (-EPIPE == n) {
			snd_pcm_prepare(st->write);

			n = snd_pcm_writei(st->write, sampv, samples);
			if (n < 0) {
				warning("alsa: write error: %s\n",
					snd_strerror((int) n));
			}
		}
		else if (n < 0) {
			if (st->run)
				warning("alsa: write error: %s\n",
					snd_strerror((int) n));
		}
		else if (n != samples) {
			warning("alsa: write: wrote %d of %d samples\n",
				(int) n, samples);
		}
#endif
	}

	//snd_pcm_drop(st->write);

	return NULL;
}

int hisi_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	//snd_pcm_format_t pcmfmt;
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
	st->wh  = wh;
	st->arg = arg;

	st->sampc = audio_frame_size(st->prm.srate);

	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

    int AoDevId = 0;

    AIO_ATTR_S stAioAttr = {
        .enSamplerate = st->prm.srate,
        .enBitwidth = AUDIO_BIT_WIDTH_16,
        .enWorkmode = AIO_MODE_I2S_MASTER,
        .enSoundmode = AUDIO_SOUND_MODE_MONO,
        .u32EXFlag = 0,
        .u32FrmNum = 2, // keep it small for low latency
        .u32PtNumPerFrm = st->sampc,
        .u32ChnCnt = 1,
        .u32ClkSel = 0,
        .enI2sType = AIO_I2STYPE_INNERCODEC,
    };
    int ret = HI_MPI_AO_SetPubAttr(AoDevId, &stAioAttr);
    if (HI_SUCCESS != ret) {
	    //printf("error %s\n", hi_errstr(ret));
    }

    ret = HI_MPI_AO_Enable(AoDevId);
    if (HI_SUCCESS != ret) {
	    printf("error %s\n", hi_errstr(ret));
	    return EINVAL;
    }

    ret = HI_MPI_AO_EnableChn(AoDevId, 0);
    if (HI_SUCCESS != ret) {
	printf("error %s\n", hi_errstr(ret));
	return EINVAL;
    }

    ret = HI_MPI_AO_SetVolume(AoDevId, -10);
    if (ret != HI_SUCCESS) {
	    printf("error %s\n", hi_errstr(ret));
	    return EINVAL;
    }

#if 0
	err = snd_pcm_open(&st->write, st->device, SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0) {
		warning("alsa: could not open auplay device '%s' (%s)\n",
			st->device, snd_strerror(err));
		info("consider using dmix as your default alsa device\n");
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
#endif

	st->run = true;
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("hisi: playback started (%s)\n", st->device);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
