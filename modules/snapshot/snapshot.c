/**
 * @file snapshot.c  Snapshot Video-Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "png_vf.h"
#include "sendfilename.h"


/*
 *


https://stackoverflow.com/questions/1692184/converting-epoch-time-to-real-date-time
 *
 */

/**
 * @defgroup snapshot snapshot
 *
 * Take snapshot of the video stream and save it as PNG-files
 *
 *
 * Commands:
 *
 \verbatim
 snapshot       Take video snapshot
 \endverbatim
 */


static bool flag_enc, flag_dec;
static struct timeval last = {.tv_sec=0, .tv_usec=0};
static int frameRatePerSec=5;
static bool continous_snapshot = true;

static int encode(struct vidfilt_enc_st *st, struct vidframe *frame)
{
	(void)st;

	// info("start encode snapshot\n");


	if (!frame)
		return 0;

	if (flag_enc) {
		flag_enc = false;
		png_save_vidframe(frame, "/tmp/snapshot-send");
	}

	return 0;
}


static int decode(struct vidfilt_dec_st *st, struct vidframe *frame)
{
	struct timeval current;
	float diffInMilliSecs;
	float millisecPerFrame = 1.0/(frameRatePerSec/1000.0);

	(void)st;


	if(last.tv_sec == 0 && last.tv_usec == 0){
		gettimeofday(&last, NULL);
	}

	gettimeofday(&current, NULL);

	diffInMilliSecs = ceil(current.tv_sec*1000 + current.tv_usec / 1000 -
			last.tv_sec*1000 - last.tv_usec / 1000);

	if(diffInMilliSecs < millisecPerFrame){
/*		info("no decode snapshot");
		info(" diff %f\n",diffInSecs);
		info(" rate %d\n", frameRatePerSec);
*/
		return 0;
	}

	if (!frame)
		return 0;

	if (continous_snapshot || flag_dec) {
		flag_dec = false;
		png_save_vidframe(frame, "/tmp/snapshot-recv");
		last = current;
	}

	return 0;
}


static int do_snapshot(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	/* NOTE: not re-entrant */
	flag_enc = flag_dec = true;

	info("snapshot enabled\n");
	socket4video=-1;
//	socket4video = socket_connect();
	return 0;
}


static struct vidfilt snapshot = {
	LE_INIT, "snapshot", NULL, encode, NULL, decode,
};


static const struct cmd cmdv[] = {
	{"snapshot", 0, 0, "Take video snapshot", do_snapshot },
};


static int module_init(void)
{
	vidfilt_register(baresip_vidfiltl(), &snapshot);
	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	vidfilt_unregister(&snapshot);
	cmd_unregister(baresip_commands(), cmdv);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(snapshot) = {
	"snapshot",
	"vidfilt",
	module_init,
	module_close
};
