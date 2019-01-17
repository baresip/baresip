/**
 * @file vidinfo.c  Video-info filter
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vidinfo.h"


/**
 * @defgroup vidinfo vidinfo
 *
 * Display video-info overlay on the encode/decode streams
 *
 * Displays info like framerate and packet timing, this is mainly
 * for development and debugging.
 */


struct vidinfo_enc {
	struct vidfilt_enc_st vf;  /* base member (inheritance) */

	struct panel *panel;
};


struct vidinfo_dec {
	struct vidfilt_dec_st vf;  /* base member (inheritance) */

	struct panel *panel;
};


static void encode_destructor(void *arg)
{
	struct vidinfo_enc *st = arg;

	list_unlink(&st->vf.le);
	mem_deref(st->panel);
}


static void decode_destructor(void *arg)
{
	struct vidinfo_dec *st = arg;

	list_unlink(&st->vf.le);
	mem_deref(st->panel);
}


static int encode_update(struct vidfilt_enc_st **stp, void **ctx,
			 const struct vidfilt *vf)
{
	struct vidinfo_enc *st;
	int err = 0;

	if (!stp || !ctx || !vf)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	if (err)
		mem_deref(st);
	else
		*stp = (struct vidfilt_enc_st *)st;

	return err;
}


static int decode_update(struct vidfilt_dec_st **stp, void **ctx,
			 const struct vidfilt *vf)
{
	struct vidinfo_dec *st;
	int err = 0;

	if (!stp || !ctx || !vf)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	if (err)
		mem_deref(st);
	else
		*stp = (struct vidfilt_dec_st *)st;

	return err;
}


static int encode(struct vidfilt_enc_st *_st, struct vidframe *frame,
		  uint64_t *timestamp)
{
	struct vidinfo_enc *st = (struct vidinfo_enc *)_st;
	int err = 0;
	(void)timestamp;

	if (!st->panel) {

		unsigned width = frame->size.w;
		unsigned height = MIN(PANEL_HEIGHT, frame->size.h);

		err = panel_alloc(&st->panel, "encode", 0, width, height);
		if (err)
			return err;
	}

	panel_add_frame(st->panel, tmr_jiffies());

	panel_draw(st->panel, frame);

	return 0;
}


static int decode(struct vidfilt_dec_st *_st, struct vidframe *frame,
		  uint64_t *timestamp)
{
	struct vidinfo_dec *st = (struct vidinfo_dec *)_st;
	int err = 0;
	(void)timestamp;

	if (!st->panel) {

		unsigned width = frame->size.w;
		unsigned height = MIN(PANEL_HEIGHT, frame->size.h);
		unsigned yoffs = frame->size.h - PANEL_HEIGHT;

		err = panel_alloc(&st->panel, "decode", yoffs, width, height);
		if (err)
			return err;
	}

	panel_add_frame(st->panel, tmr_jiffies());

	panel_draw(st->panel, frame);

	return 0;
}


static struct vidfilt vidinfo = {
	LE_INIT, "vidinfo", encode_update, encode, decode_update, decode
};


static int module_init(void)
{
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
