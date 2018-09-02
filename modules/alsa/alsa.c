/**
 * @file alsa.c  ALSA sound driver
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
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "alsa.h"


/**
 * @defgroup alsa alsa
 *
 * Advanced Linux Sound Architecture (ALSA) audio driver module
 *
 *
 * References:
 *
 *    http://www.alsa-project.org/main/index.php/Main_Page
 */


char alsa_dev[64] = "default";

static struct ausrc *ausrc;
static struct auplay *auplay;


int alsa_reset(snd_pcm_t *pcm, uint32_t srate, uint32_t ch,
	       uint32_t num_frames,
	       snd_pcm_format_t pcmfmt)
{
	snd_pcm_hw_params_t *hw_params = NULL;
	snd_pcm_uframes_t period = num_frames, bufsize = num_frames * 4;
	int err;

	debug("alsa: reset: srate=%u, ch=%u, num_frames=%u, pcmfmt=%s\n",
	      srate, ch, num_frames, snd_pcm_format_name(pcmfmt));

	err = snd_pcm_hw_params_malloc(&hw_params);
	if (err < 0) {
		warning("alsa: cannot allocate hw params (%s)\n",
			snd_strerror(err));
		goto out;
	}

	err = snd_pcm_hw_params_any(pcm, hw_params);
	if (err < 0) {
		warning("alsa: cannot initialize hw params (%s)\n",
			snd_strerror(err));
		goto out;
	}

	err = snd_pcm_hw_params_set_access(pcm, hw_params,
					   SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		warning("alsa: cannot set access type (%s)\n",
			snd_strerror(err));
		goto out;
	}

	err = snd_pcm_hw_params_set_format(pcm, hw_params, pcmfmt);
	if (err < 0) {
		warning("alsa: cannot set sample format %d (%s)\n",
			pcmfmt, snd_strerror(err));
		goto out;
	}

	err = snd_pcm_hw_params_set_rate(pcm, hw_params, srate, 0);
	if (err < 0) {
		warning("alsa: cannot set sample rate to %u Hz (%s)\n",
			srate, snd_strerror(err));
		goto out;
	}

	err = snd_pcm_hw_params_set_channels(pcm, hw_params, ch);
	if (err < 0) {
		warning("alsa: cannot set channel count to %d (%s)\n",
			ch, snd_strerror(err));
		goto out;
	}

	err = snd_pcm_hw_params_set_period_size_near(pcm, hw_params,
						     &period, 0);
	if (err < 0) {
		warning("alsa: cannot set period size to %d (%s)\n",
			period, snd_strerror(err));
	}

	err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw_params, &bufsize);
	if (err < 0) {
		warning("alsa: cannot set buffer size to %d (%s)\n",
			bufsize, snd_strerror(err));
	}

	err = snd_pcm_hw_params(pcm, hw_params);
	if (err < 0) {
		warning("alsa: cannot set parameters (%s)\n",
			snd_strerror(err));
		goto out;
	}

	err = snd_pcm_prepare(pcm);
	if (err < 0) {
		warning("alsa: cannot prepare audio interface for use (%s)\n",
			snd_strerror(err));
		goto out;
	}

	err = 0;

 out:
	snd_pcm_hw_params_free(hw_params);

	if (err) {
		warning("alsa: init failed: err=%d\n", err);
	}

	return err;
}


snd_pcm_format_t aufmt_to_alsaformat(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:    return SND_PCM_FORMAT_S16;
	case AUFMT_FLOAT:    return SND_PCM_FORMAT_FLOAT;
	case AUFMT_S24_3LE:  return SND_PCM_FORMAT_S24_3LE;
	default:             return SND_PCM_FORMAT_UNKNOWN;
	}
}


static int alsa_init(void)
{
	int err;

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "alsa", alsa_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "alsa", alsa_play_alloc);

	return err;
}


static int alsa_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	/* releases all resources of the global configuration tree,
	   and sets snd_config to NULL. */
	snd_config_update_free_global();

	return 0;
}


const struct mod_export DECL_EXPORTS(alsa) = {
	"alsa",
	"sound",
	alsa_init,
	alsa_close
};
