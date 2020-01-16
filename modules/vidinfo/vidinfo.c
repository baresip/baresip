/**
 * @file vidinfo.c  Video-info filter
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vidinfo.h"
#include "xga_font_data.h"


/**
 * @defgroup vidinfo vidinfo
 *
 * Display video-info overlay on the encode/decode streams
 *
 * Displays info like framerate and packet timing, this is mainly
 * for development and debugging.
 */


#define MAX_CHARS_WIDTH  32
#define MAX_CHARS_HEIGHT 10

#define MAX_PIXELS_WIDTH   (MAX_CHARS_WIDTH  * FONT_WIDTH)
#define MAX_PIXELS_HEIGHT  (MAX_CHARS_HEIGHT * FONT_HEIGHT)


enum layout {
	LAYOUT_TOP,
	LAYOUT_BOTTOM,
};


struct vidinfo_dec {
	struct vidfilt_dec_st vf;  /* base member (inheritance) */

	struct stats stats;
	const struct video *vid;
};


static enum layout box_layout = LAYOUT_TOP;


static void decode_destructor(void *arg)
{
	struct vidinfo_dec *st = arg;

	list_unlink(&st->vf.le);
}


static int decode_update(struct vidfilt_dec_st **stp, void **ctx,
			 const struct vidfilt *vf, struct vidfilt_prm *prm,
			 const struct video *vid)
{
	struct vidinfo_dec *st;
	(void)prm;
	(void)vid;

	if (!stp || !ctx || !vf)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->vid = vid;

	*stp = (struct vidfilt_dec_st *)st;

	return 0;
}


static int decode(struct vidfilt_dec_st *_st, struct vidframe *frame,
		  uint64_t *timestamp)
{
	struct vidinfo_dec *st = (struct vidinfo_dec *)(void *)_st;

	if (!st)
		return EINVAL;

	if (frame && timestamp) {

		unsigned x0, y0;

		if (frame->fmt != VID_FMT_YUV420P)
			return ENOTSUP;

		switch (box_layout) {

		case LAYOUT_TOP:
			x0 = 4;
			y0 = 4;
			break;

		case LAYOUT_BOTTOM:
			x0 = 4;
			y0 = frame->size.h - MAX_PIXELS_HEIGHT;
			break;

		default:
			return EINVAL;
		}

		vidinfo_draw_box(frame, *timestamp, &st->stats, st->vid,
				 x0, y0, MAX_PIXELS_WIDTH, MAX_PIXELS_HEIGHT);

		st->stats.last_timestamp = *timestamp;
	}

	return 0;
}


static struct vidfilt vidinfo = {
	.name    = "vidinfo",
	.decupdh = decode_update,
	.dech    = decode,
};


static int module_init(void)
{
	struct pl pl;

	if (0 == conf_get(conf_cur(), "vidinfo_layout", &pl)) {

		if (0 == pl_strcasecmp(&pl, "top")) {

			box_layout = LAYOUT_TOP;
		}
		else if (0 == pl_strcasecmp(&pl, "bottom")) {

			box_layout = LAYOUT_BOTTOM;
		}
	}

	vidfilt_register(baresip_vidfiltl(), &vidinfo);

	return 0;
}


static int module_close(void)
{
	vidfilt_unregister(&vidinfo);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vidinfo) = {
	"vidinfo",
	"vidfilt",
	module_init,
	module_close
};
