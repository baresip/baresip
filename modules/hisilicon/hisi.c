/**
 * @file alsa.c  ALSA sound driver
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _POSIX_SOURCE 1
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "hisi.h"
#include "errors.h"

#include <mpi_sys.h>
#include <mpi_vb.h>

char alsa_dev[64] = "default";

static struct ausrc *ausrc;
static struct auplay *auplay;


__attribute__((visibility("default"))) int __fgetc_unlocked(FILE *stream) {
    return fgetc(stream);
}

__attribute__((visibility("default"))) size_t _stdlib_mb_cur_max(void) {
    return 0;
}

__attribute__((visibility("default"))) const unsigned short int *__ctype_b;

#if 0
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
#endif


#if 0
snd_pcm_format_t aufmt_to_alsaformat(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:    return SND_PCM_FORMAT_S16;
	case AUFMT_FLOAT:    return SND_PCM_FORMAT_FLOAT;
	case AUFMT_S24_3LE:  return SND_PCM_FORMAT_S24_3LE;
	default:             return SND_PCM_FORMAT_UNKNOWN;
	}
}
#endif

unsigned audio_frame_size(unsigned srate) {
// Set Opus-compatible frame sizes. See
// <https://github.com/xiph/opus/issues/228>.
#define ASSOC(srate)                                                           \
    case (srate):                                                              \
        return (srate) / 50;

    switch (srate) {
        ASSOC(8000);
        ASSOC(12000);
        ASSOC(16000);
        ASSOC(24000);
        ASSOC(48000);
    default:
        return 320;
    }

#undef ASSOC
}

static void init_hw() {
    int ret = HI_MPI_SYS_Init();

    if (HI_SUCCESS != ret)
    {
        printf("HI_MPI_SYS_Init failed with %s\n", hi_errstr(ret));
        HI_MPI_VB_Exit();
    }
}


static int hisi_init(void)
{
	int err;

	init_hw();

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "hisilicon", hisi_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "hisilicon", hisi_play_alloc);

	return err;
}


static int hisi_close(void)
{
	ausrc  = mem_deref(ausrc);
	auplay = mem_deref(auplay);

#if 0
	/* releases all resources of the global configuration tree,
	   and sets snd_config to NULL. */
	snd_config_update_free_global();
#endif

	return 0;
}


const struct mod_export DECL_EXPORTS(hisilicon) = {
	"hisilicon",
	"sound",
	hisi_init,
	hisi_close
};
