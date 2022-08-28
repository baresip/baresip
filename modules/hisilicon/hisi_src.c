/**
 * @file hisi.c  HiSilicon sound driver - recorder
 *
 * Copyright (C) 2022 Dmitry Ilyin
 */
#include "hi_type.h"
#define _DEFAULT_SOURCE 1
#define _POSIX_SOURCE 1
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "hisi.h"
#include "errors.h"

#include <mpi_audio.h>

struct ausrc_st {
	pthread_t thread;
	volatile bool run;
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
	if (st->run) {
		debug("hisi: stopping recording thread (%s)\n", st->device);
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	int ret = HI_MPI_AI_DisableChn(0, 0);
	if (HI_SUCCESS != ret) {
		printf("error %s\n", hi_errstr(ret));
	}

	ret = HI_MPI_AI_Disable(0);
	if (HI_SUCCESS != ret) {
		printf("error %s\n", hi_errstr(ret));
	}

	mem_deref(st->sampv);
	mem_deref(st->device);
}

static void *read_thread(void *arg)
{
	AEC_FRAME_S stAecFrm;
	AUDIO_FRAME_S stFrame;

	int s32Ret;
	int AiDev = 0;
	int AiChn = 0;

	struct ausrc_st *st = arg;
	int err;

	while (st->run) {
		memset(&stAecFrm, 0, sizeof(AEC_FRAME_S));
		s32Ret = HI_MPI_AI_GetFrame(AiDev, AiChn, &stFrame, &stAecFrm, -1);
		if (HI_SUCCESS != s32Ret) {
			printf("%s: HI_MPI_AI_GetFrame(%d, %d), failed with %#x!\n", __FUNCTION__, AiDev, AiChn, s32Ret);
			continue;
		}

		struct auframe af;

		memcpy(st->sampv, stFrame.u64VirAddr[0], stFrame.u32Len);
		s32Ret = HI_MPI_AI_ReleaseFrame(AiDev, AiChn, &stFrame, &stAecFrm);
		if (HI_SUCCESS != s32Ret) {
			printf("%s: HI_MPI_AI_ReleaseFrame(%d, %d), failed with %#x!\n", __FUNCTION__, AiDev, AiChn, s32Ret);
			continue;
		}

		auframe_init(&af, AUFMT_S16LE, st->sampv, stFrame.u32Len / 2, st->prm.srate, 1);
		af.timestamp = stFrame.u64TimeStamp;

		st->rh(&af, st->arg);
	}

 out:
	return NULL;
}


#include <fcntl.h>
#include <sys/ioctl.h>
#include <acodec.h>

#define ACODEC_FILE "/dev/acodec"

HI_S32 AUDIO_CfgAcodec(AUDIO_SAMPLE_RATE_E enSample) {
    HI_S32 fdAcodec = -1;
    HI_S32 ret = HI_SUCCESS;
    ACODEC_FS_E i2s_fs_sel = 0;
    int iAcodecInputVol = 0;
    ACODEC_MIXER_E input_mode = 0;

    // override default value to make it great for XM boards
    if (iAcodecInputVol == 0)
        iAcodecInputVol = 50;

    fdAcodec = open(ACODEC_FILE, O_RDWR);
    if (fdAcodec < 0) {
        printf("Can't open Acodec: %s\n", ACODEC_FILE);
        return HI_FAILURE;
    }
    if (ioctl(fdAcodec, ACODEC_SOFT_RESET_CTRL)) {
        printf("Reset audio codec error\n");
    }

    switch (enSample) {
    case AUDIO_SAMPLE_RATE_8000:
        i2s_fs_sel = ACODEC_FS_8000;
        break;

    case AUDIO_SAMPLE_RATE_16000:
        i2s_fs_sel = ACODEC_FS_16000;
        break;

    case AUDIO_SAMPLE_RATE_32000:
        i2s_fs_sel = ACODEC_FS_32000;
        break;

    case AUDIO_SAMPLE_RATE_11025:
        i2s_fs_sel = ACODEC_FS_11025;
        break;

    case AUDIO_SAMPLE_RATE_22050:
        i2s_fs_sel = ACODEC_FS_22050;
        break;

    case AUDIO_SAMPLE_RATE_44100:
        i2s_fs_sel = ACODEC_FS_44100;
        break;

    case AUDIO_SAMPLE_RATE_12000:
        i2s_fs_sel = ACODEC_FS_12000;
        break;

    case AUDIO_SAMPLE_RATE_24000:
        i2s_fs_sel = ACODEC_FS_24000;
        break;

    case AUDIO_SAMPLE_RATE_48000:
        i2s_fs_sel = ACODEC_FS_48000;
        break;

    case AUDIO_SAMPLE_RATE_64000:
        i2s_fs_sel = ACODEC_FS_64000;
        break;

    case AUDIO_SAMPLE_RATE_96000:
        i2s_fs_sel = ACODEC_FS_96000;
        break;

    default:
        printf("not support enSample: %d\n", enSample);
        ret = HI_FAILURE;
        break;
    }

    if (ioctl(fdAcodec, ACODEC_SET_I2S1_FS, &i2s_fs_sel)) {
        printf("set acodec sample rate failed\n");
        ret = HI_FAILURE;
    }

    input_mode = ACODEC_MIXER_IN1;
    if (ioctl(fdAcodec, ACODEC_SET_MIXER_MIC, &input_mode)) {
        printf("select acodec input_mode failed\n");
        ret = HI_FAILURE;
    }

    if (iAcodecInputVol != 0) /* should be 1 when micin */
    {
        /******************************************************************************************
        The input volume range is [-87, +86]. Both the analog gain and digital
        gain are adjusted. A larger value indicates higher volume. For example,
        the value 86 indicates the maximum volume of 86 dB, and the value -87
        indicates the minimum volume (muted status). The volume adjustment takes
        effect simultaneously in the audio-left and audio-right channels. The
        recommended volume range is [+10, +56]. Within this range, the noises
        are lowest because only the analog gain is adjusted, and the voice
        quality can be guaranteed.
        *******************************************************************************************/
        if (ioctl(fdAcodec, ACODEC_SET_INPUT_VOL, &iAcodecInputVol)) {
            printf("set acodec micin volume failed\n");
            return HI_FAILURE;
        }
    }

    close(fdAcodec);
    return ret;
}


int hisi_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		   struct ausrc_prm *prm, const char *device,
		   ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
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

	st->sampc = audio_frame_size(st->prm.srate);

	st->sampv = mem_alloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

    AUDIO_CfgAcodec(st->prm.srate);

    int AiDevId = 0;
    AIO_ATTR_S stAioAttr = {
        .enSamplerate = st->prm.srate,
        .enBitwidth = AUDIO_BIT_WIDTH_16,
        .enWorkmode = AIO_MODE_I2S_MASTER,
        .enSoundmode = AUDIO_SOUND_MODE_MONO,
        .u32EXFlag = 0,
        .u32FrmNum = 2,
        .u32PtNumPerFrm = st->sampc,
        .u32ChnCnt = 1,
        .u32ClkSel = 0,
        .enI2sType = AIO_I2STYPE_INNERCODEC,
    };
    int ret = HI_MPI_AI_SetPubAttr(AiDevId, &stAioAttr);
    if (HI_SUCCESS != ret) {
	    //printf("error %s\n", hi_errstr(ret));
    }

    ret = HI_MPI_AI_Enable(AiDevId);
    if (HI_SUCCESS != ret) {
	    printf("error %s\n", hi_errstr(ret));
	    return EINVAL;
    }

    ret = HI_MPI_AI_EnableChn(AiDevId, 0);
    if (HI_SUCCESS != ret) {
	printf("error %s\n", hi_errstr(ret));
	return EINVAL;
    }

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("hisi: recording started (%s) format=%s\n",
	      st->device, aufmt_name(prm->fmt));

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
