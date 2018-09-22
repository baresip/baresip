/**
 * @file selfview.c  Selfview Video-Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup selfview selfview
 *
 * Show a selfview of the captured video stream
 *
 * Example config:
 \verbatim
  video_selfview          pip # {window,pip}
  selfview_size           64x64
 \endverbatim
 */


/* shared state */
struct selfview {
	struct lock *lock;          /**< Protect frame         */
	struct vidframe *frame;     /**< Copy of encoded frame */
};

struct selfview_enc {
	struct vidfilt_enc_st vf;   /**< Inheritance           */
	struct selfview *selfview;  /**< Ref. to shared state  */
	struct vidisp_st *disp;     /**< Selfview display      */
};

struct selfview_dec {
	struct vidfilt_dec_st vf;   /**< Inheritance           */
	struct selfview *selfview;  /**< Ref. to shared state  */
};


static struct vidsz selfview_size = {0, 0};


static void destructor(void *arg)
{
	struct selfview *st = arg;

	lock_write_get(st->lock);
	mem_deref(st->frame);
	lock_rel(st->lock);
	mem_deref(st->lock);
}


static void encode_destructor(void *arg)
{
	struct selfview_enc *st = arg;

	list_unlink(&st->vf.le);
	mem_deref(st->selfview);
	mem_deref(st->disp);
}


static void decode_destructor(void *arg)
{
	struct selfview_dec *st = arg;

	list_unlink(&st->vf.le);
	mem_deref(st->selfview);
}


static int selfview_alloc(struct selfview **selfviewp, void **ctx)
{
	struct selfview *selfview;
	int err;

	if (!selfviewp || !ctx)
		return EINVAL;

	if (*ctx) {
		*selfviewp = mem_ref(*ctx);
	}
	else {
		selfview = mem_zalloc(sizeof(*selfview), destructor);
		if (!selfview)
			return ENOMEM;

		err = lock_alloc(&selfview->lock);
		if (err)
			return err;

		*ctx = selfview;
		*selfviewp = selfview;
	}

	return 0;
}


static int encode_update(struct vidfilt_enc_st **stp, void **ctx,
			 const struct vidfilt *vf)
{
	struct selfview_enc *st;
	int err;

	if (!stp || !ctx || !vf)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	err = selfview_alloc(&st->selfview, ctx);

	if (err)
		mem_deref(st);
	else
		*stp = (struct vidfilt_enc_st *)st;

	return err;
}


static int decode_update(struct vidfilt_dec_st **stp, void **ctx,
			 const struct vidfilt *vf)
{
	struct selfview_dec *st;
	int err;

	if (!stp || !ctx || !vf)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	err = selfview_alloc(&st->selfview, ctx);

	if (err)
		mem_deref(st);
	else
		*stp = (struct vidfilt_dec_st *)st;

	return err;
}


static int encode_win(struct vidfilt_enc_st *st, struct vidframe *frame,
		      uint64_t *timestamp)
{
	struct selfview_enc *enc = (struct selfview_enc *)st;
	int err;

	if (!frame)
		return 0;

	if (!enc->disp) {

		err = vidisp_alloc(&enc->disp, baresip_vidispl(),
				   NULL, NULL, NULL, NULL, NULL);
		if (err)
			return err;
	}

	return vidisp_display(enc->disp, "Selfview", frame, *timestamp);
}


static int encode_pip(struct vidfilt_enc_st *st, struct vidframe *frame,
		      uint64_t *timestamp)
{
	struct selfview_enc *enc = (struct selfview_enc *)st;
	struct selfview *selfview = enc->selfview;
	int err = 0;
	(void)timestamp;

	if (!frame)
		return 0;

	lock_write_get(selfview->lock);
	if (!selfview->frame) {
		struct vidsz sz;

		/* Use size if configured, or else 20% of main window */
		if (selfview_size.w && selfview_size.h) {
			sz = selfview_size;
		}
		else {
			sz.w = frame->size.w / 5;
			sz.h = frame->size.h / 5;
		}

		err = vidframe_alloc(&selfview->frame, VID_FMT_YUV420P, &sz);
	}
	if (!err)
		vidconv(selfview->frame, frame, NULL);
	lock_rel(selfview->lock);

	return err;
}


static int decode_pip(struct vidfilt_dec_st *st, struct vidframe *frame,
		      uint64_t *timestamp)
{
	struct selfview_dec *dec = (struct selfview_dec *)st;
	struct selfview *sv = dec->selfview;
	(void)timestamp;

	if (!frame)
		return 0;

	lock_read_get(sv->lock);
	if (sv->frame) {
		struct vidrect rect;

		rect.w = min(sv->frame->size.w, frame->size.w/2);
		rect.h = min(sv->frame->size.h, frame->size.h/2);
		if (rect.w <= (frame->size.w - 10))
			rect.x = frame->size.w - rect.w - 10;
		else
			rect.x = frame->size.w/2;
		if (rect.h <= (frame->size.h - 10))
			rect.y = frame->size.h - rect.h - 10;
		else
			rect.y = frame->size.h/2;

		vidconv(frame, sv->frame, &rect);

		vidframe_draw_rect(frame, rect.x, rect.y, rect.w, rect.h,
				   127, 127, 127);
	}
	lock_rel(sv->lock);

	return 0;
}


static struct vidfilt selfview_win = {
	LE_INIT, "selfview_window", encode_update, encode_win, NULL, NULL
};
static struct vidfilt selfview_pip = {
	LE_INIT, "selfview_pip",
	encode_update, encode_pip, decode_update, decode_pip
};


static int module_init(void)
{
	struct pl pl = PL("pip");

	(void)conf_get(conf_cur(), "video_selfview", &pl);

	if (0 == pl_strcasecmp(&pl, "window"))
		vidfilt_register(baresip_vidfiltl(), &selfview_win);
	else if (0 == pl_strcasecmp(&pl, "pip"))
		vidfilt_register(baresip_vidfiltl(), &selfview_pip);

	(void)conf_get_vidsz(conf_cur(), "selfview_size", &selfview_size);

	return 0;
}


static int module_close(void)
{
	vidfilt_unregister(&selfview_win);
	vidfilt_unregister(&selfview_pip);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(selfview) = {
	"selfview",
	"vidfilt",
	module_init,
	module_close
};
