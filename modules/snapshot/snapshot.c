/**
 * @file snapshot.c  Snapshot Video-Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "png_vf.h"
#include <time.h>

/**
 * @defgroup snapshot snapshot
 *
 * Take snapshot of the video stream and save it as PNG-files
 *
 *
 * Commands:
 *
 \verbatim
 snapshot           Take video snapshot of both video streams
 snapshot_recv path Take snapshot of receiving video and save it to the path
 snapshot_send path Take snapshot of sending video and save it to the path
 \endverbatim
 */


static bool flag_enc, flag_dec;
static char path_enc[100], path_dec[100];

static char *png_filename(const struct tm *tmx, const char *name,
			char *buf, unsigned int length);

static int encode(struct vidfilt_enc_st *st, struct vidframe *frame,
			uint64_t *timestamp)
{
	(void)st;
	(void)timestamp;

	if (!frame)
		return 0;

	if (flag_enc) {
		flag_enc = false;
		png_save_vidframe(frame, path_enc);
	}

	return 0;
}


static int decode(struct vidfilt_dec_st *st, struct vidframe *frame,
			uint64_t *timestamp)
{
	(void)st;
	(void)timestamp;

	if (!frame)
		return 0;

	if (flag_dec) {
		flag_dec = false;
		png_save_vidframe(frame, path_dec);
	}

	return 0;
}


static int do_snapshot(struct re_printf *pf, void *arg)
{
	time_t tnow;
	struct tm *tmx;

	(void)pf;
	(void)arg;

	if (flag_enc || flag_dec)
		return 0;

	tnow = time(NULL);
	tmx = localtime(&tnow);

	/* NOTE: not re-entrant */
	png_filename(tmx, "snapshot-recv", path_dec, sizeof(path_dec));
	png_filename(tmx, "snapshot-send", path_enc, sizeof(path_enc));
	flag_enc = flag_dec = true;

	return 0;
}

static int do_snapshot_recv(struct re_printf *pf, void *arg)
{
	(void)pf;
	const struct cmd_arg *carg = arg;

	if (flag_dec)
		return 0;

	/* NOTE: not re-entrant */
	str_ncpy(path_dec, carg->prm, sizeof(path_dec));
	flag_dec = true;

	return 0;
}

static int do_snapshot_send(struct re_printf *pf, void *arg)
{
	(void)pf;
	const struct cmd_arg *carg = arg;

	if (flag_enc)
		return 0;

	/* NOTE: not re-entrant */
	str_ncpy(path_enc, carg->prm, sizeof(path_enc));
	flag_enc = true;

	return 0;
}

static struct vidfilt snapshot = {
	.name = "snapshot",
	.ench = encode,
	.dech = decode,
};


static const struct cmd cmdv[] = {
	{"snapshot", 0, 0, "Take video snapshot", do_snapshot },
	{"snapshot_recv", 0, CMD_PRM,
   "Take receiving video snapshot and save to path",
   do_snapshot_recv},
	{"snapshot_send", 0, CMD_PRM,
   "Take sending video snapshot and save to path",
   do_snapshot_send}
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


static char *png_filename(const struct tm *tmx, const char *name,
			char *buf, unsigned int length)
{
	/*
	 * -2013-03-03-15-22-56.png - 24 chars
	 */
	if (strlen(name) + 24 >= length) {
		buf[0] = '\0';
		return buf;
	}

	sprintf(buf, (tmx->tm_mon < 9 ? "%s-%d-0%d" : "%s-%d-%d"), name,
					1900 + tmx->tm_year, tmx->tm_mon + 1);

	sprintf(buf + strlen(buf), (tmx->tm_mday < 10 ? "-0%d" : "-%d"),
					tmx->tm_mday);

	sprintf(buf + strlen(buf), (tmx->tm_hour < 10 ? "-0%d" : "-%d"),
					tmx->tm_hour);

	sprintf(buf + strlen(buf), (tmx->tm_min < 10 ? "-0%d" : "-%d"),
					tmx->tm_min);

	sprintf(buf + strlen(buf), (tmx->tm_sec < 10 ? "-0%d.png" : "-%d.png"),
					tmx->tm_sec);

	return buf;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(snapshot) = {
	"snapshot",
	"vidfilt",
	module_init,
	module_close
};
