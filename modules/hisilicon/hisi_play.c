/**
 * @file hisi_play.c  HiSilicon sound driver - player
 *
 * Copyright (C) 2022 Dmitry Ilyin
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <threads.h>

#include "hisi.h"

#include <mpi_audio.h>
#include <mpi_sys.h>

struct auplay_st {
	thrd_t thread;
	volatile bool run;
	int16_t *sampv;
	size_t sampc;
	auplay_write_h *wh;
	void *arg;
	struct auplay_prm prm;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		debug("hisi: stopping playback thread\n");
		st->run = false;
		thrd_join(st->thread, NULL);
	}

	int ret = HI_MPI_AO_DisableChn(0, 0);
	if (HI_SUCCESS != ret) {
		warning("hisi: error %d\n", ret);
	}

	ret = HI_MPI_AO_Disable(0);
	if (HI_SUCCESS != ret) {
		warning("hisi: error %d\n", ret);
	}

	mem_deref(st->sampv);
}


static int write_thread(void *arg)
{
	struct auplay_st *st = arg;
	struct auframe af;

	auframe_init(&af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate,
			st->prm.ch);

	while (st->run) {
		st->wh(&af, st->arg);

		AUDIO_FRAME_S stData = {
			.enBitwidth = AUDIO_BIT_WIDTH_16,
			.enSoundmode = AUDIO_SOUND_MODE_MONO,
			.u32Len = st->sampc * sizeof(int16_t),
			.u64VirAddr[0] = (uint8_t*)st->sampv,
		};
		int ret = HI_MPI_AO_SendFrame(0, 0, &stData, -1);
		if (ret != HI_SUCCESS) {
			warning("hisi: error %d\n", ret);
		}

	}

	return 0;
}

int hisi_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		struct auplay_prm *prm, const char *device,
		auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err = 0;
	int ret;
	(void)device;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

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
		.u32FrmNum = 2, /* keep it small for low latency */
		.u32PtNumPerFrm = st->sampc,
		.u32ChnCnt = 1,
		.u32ClkSel = 0,
		.enI2sType = AIO_I2STYPE_INNERCODEC,
	};
	HI_MPI_AO_SetPubAttr(AoDevId, &stAioAttr);

	ret = HI_MPI_AO_Enable(AoDevId);
	if (HI_SUCCESS != ret) {
		warning("hisi: error %d\n", ret);
		return EINVAL;
	}

	ret = HI_MPI_AO_EnableChn(AoDevId, 0);
	if (HI_SUCCESS != ret) {
		warning("hisi: error %d\n", ret);
		return EINVAL;
	}

	ret = HI_MPI_AO_SetVolume(AoDevId, -10);
	if (HI_SUCCESS != ret) {
		warning("hisi: error %d\n", ret);
		return EINVAL;
	}

	st->run = true;
	if (thrd_success != thrd_create(&st->thread, write_thread, st)) {
		err = EAGAIN;
		st->run = false;
		goto out;
	}

	debug("hisi: playback started\n");

out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}