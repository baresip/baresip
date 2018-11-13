/**
 * @file winwave.c Windows sound driver
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <baresip.h>
#include "winwave.h"


/**
 * @defgroup winwave winwave
 *
 * Windows audio driver module
 *
 */


static struct ausrc *ausrc;
static struct auplay *auplay;


int winwave_enum_devices(const char *name, struct list *dev_list,
			 unsigned int *dev,
			 unsigned int (winwave_get_num_devs)(void),
			 int (winwave_get_dev_name)(unsigned int, char*))
{
	/* The szPname member of the WAVEINCAPS/WAVEOUTCAPS structures
	   is limited to MAXPNAMELEN characters, which is defined as 32 */
	char dev_name[32];
	int err = 0;
	unsigned int i, nDevices = winwave_get_num_devs();


	if (!dev_list && !dev)
		return EINVAL;

	if (dev) {
		*dev = WAVE_MAPPER;

		if (!str_isset(name)) {
			return 0;
		}
	}

	for (i=0; i<nDevices; i++) {

		err = winwave_get_dev_name (i, dev_name);
		if (err) {
			break;
		}

		if (dev) {
			if (!str_casecmp(name, dev_name)) {
				*dev = i;
				break;
			}
		}
		else {
			err = mediadev_add(dev_list, dev_name);
			if (err) {
				break;
			}
		}
	}

	return err;
}


unsigned winwave_get_format(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:   return WAVE_FORMAT_PCM;
	case AUFMT_FLOAT:   return WAVE_FORMAT_IEEE_FLOAT;
	default:            return WAVE_FORMAT_UNKNOWN;
	}
}


static int ww_init(void)
{
	int play_dev_count, src_dev_count;
	int err;

	play_dev_count = waveOutGetNumDevs();
	src_dev_count = waveInGetNumDevs();

	info("winwave: output devices: %d, input devices: %d\n",
	     play_dev_count, src_dev_count);

	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "winwave", winwave_src_alloc);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "winwave", winwave_play_alloc);

	if (err)
		return err;

	err  = winwave_player_init(auplay);
	err |= winwave_src_init(ausrc);

	return err;
}


static int ww_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(winwave) = {
	"winwave",
	"sound",
	ww_init,
	ww_close
};
