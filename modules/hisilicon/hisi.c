/**
 * @file hisi.c  HiSilicon sound driver
 *
 * Copyright (C) 2022 Dmitry Ilyin
 */

#include <re.h>
#include <baresip.h>

#include "hisi.h"

#include <mpi_sys.h>
#include <mpi_vb.h>

static struct ausrc *ausrc;
static struct auplay *auplay;

static int init_hw(void)
{
	int ret = HI_MPI_SYS_Init();

	if (HI_SUCCESS != ret) {
		warning("hisi: HI_MPI_SYS_Init failed with %d\n", ret);
		HI_MPI_VB_Exit();
	}
	return ret;
}

unsigned audio_frame_size(unsigned srate)
{
	switch (srate) {
		case 8000: return 8000 / 50;
		case 12000: return 12000 / 50;
		case 16000: return 16000 / 50;
		case 24000: return 24000 / 50;
		case 48000: return 48000 / 50;
		default:
			    return 320;
	}
}

static int hisi_init(void)
{
	int err;

	if (init_hw() == HI_FAILURE) {
		return EINVAL;
	}

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

	HI_MPI_SYS_Exit();

	return 0;
}


const struct mod_export DECL_EXPORTS(hisilicon) = {
	"hisilicon",
	"sound",
	hisi_init,
	hisi_close
};
