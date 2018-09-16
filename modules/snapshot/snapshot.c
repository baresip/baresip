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


static int encode(struct vidfilt_enc_st *st, struct vidframe *frame,
		  uint64_t *timestamp)
{
	(void)st;
	(void)timestamp;

	if (!frame)
		return 0;

	if (flag_enc) {
		flag_enc = false;
		png_save_vidframe(frame, "snapshot-send");
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
		png_save_vidframe(frame, "snapshot-recv");
	}

	return 0;
}


static int do_snapshot(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	/* NOTE: not re-entrant */
	flag_enc = flag_dec = true;

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
