/**
 * @file avfilter.c	 Video filter using libavfilter
 *
 * Copyright (C) 2020 Mikhail Kurkov
 */

#include <string.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "avfilter.h"

/**
 * @defgroup avfilter avfilter
 *
 * Video filters using libavfilter
 *
 * This module allows to dynamically apply complex video filter graphs
 * to outcoming stream using libavfilter from FFmpeg project.
 *
 * Commands:
 *
 \verbatim
 avfilter <FILTER> - Enable avfilter for outcoming stream
 avfilter          - Disable avfilter
 \endverbatim
 *
 * Example:
 *
 \verbatim
 avfilter movie=watermark.png[pic];[in][pic]overlay=10:10[out]
 \endverbatim
 *
 * References:
 *
 *     https://ffmpeg.org/ffmpeg-filters.html
 *
 */

static struct lock *lock;
static char filter_descr[MAX_DESCR] = "";
static bool filter_updated = false;


static void st_destructor(void *arg)
{
	struct avfilter_st *st = arg;

	list_unlink(&st->vf.le);
	filter_reset(st);
}


static int update(struct vidfilt_enc_st **stp, void **ctx,
		  const struct vidfilt *vf, struct vidfilt_prm *prm,
		  const struct video *vid)
{
	struct avfilter_st *st;
	(void)vid;

	if (!stp || !ctx || !vf || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), st_destructor);
	if (!st)
		return ENOMEM;

	st->enabled = false;

	*stp = (struct vidfilt_enc_st *)st;
	return 0;
}


static int encode(struct vidfilt_enc_st *enc_st, struct vidframe *frame,
		  uint64_t *timestamp)
{
	struct avfilter_st *st = (struct avfilter_st *)enc_st;
	int err=0;
	(void)timestamp;

	if (!frame)
		return 0;

	lock_write_get(lock);
	if (filter_updated || !filter_valid(st, frame)) {
		filter_reset(st);
		filter_init(st, filter_descr, frame);
	}
	filter_updated = false;
	lock_rel(lock);

	err = filter_encode(st, frame, timestamp);

	return err;
}


static int avfilter_command(struct re_printf *pf, void *arg)
{
	(void)pf;
	const struct cmd_arg *carg = arg;

	lock_write_get(lock);

	if (str_isset(carg->prm)) {
		str_ncpy(filter_descr, carg->prm, sizeof(filter_descr));
		info("avfilter: enabled for %s\n", filter_descr);
	}
	else {
		str_ncpy(filter_descr, "", sizeof(filter_descr));
		info("avfilter: disabled\n");
	}

	filter_updated = true;

	lock_rel(lock);
	return 0;
}


static struct vidfilt avfilter = {
	.name    = "avfilter",
	.ench    = encode,
	.encupdh = update
};


static const struct cmd cmdv[] = {
	{"avfilter", 0, CMD_PRM, "Start avfilter", avfilter_command}
};


static int module_init(void)
{
	int err;
	err = lock_alloc(&lock);
	if (err)
		return err;

	vidfilt_register(baresip_vidfiltl(), &avfilter);
	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	lock = mem_deref(lock);
	vidfilt_unregister(&avfilter);
	cmd_unregister(baresip_commands(), cmdv);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avfilter) = {
	"avfilter",
	"vidfilt",
	module_init,
	module_close
};
