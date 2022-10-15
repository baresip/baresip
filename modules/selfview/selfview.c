/**
 * @file selfview.c  Selfview Video-Filter
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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
	mtx_t lock;                 /**< Protect frame         */
	struct vidframe *frame;     /**< Copy of encoded frame */
};

struct selfview_enc {
	struct vidfilt_enc_st vf;   /**< Inheritance           */
	struct selfview *selfview;  /**< Ref. to shared state  */
	const struct vidisp *vd;
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

	mtx_lock(&st->lock);
	mem_deref(st->frame);
	mtx_unlock(&st->lock);
	mtx_destroy(&st->lock);
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

		err = mtx_init(&selfview->lock, mtx_plain) != thrd_success;
		if (err)
			return ENOMEM;

		*ctx = selfview;
		*selfviewp = selfview;
	}

	return 0;
}


static int encode_update(struct vidfilt_enc_st **stp, void **ctx,
			 const struct vidfilt *vf, struct vidfilt_prm *prm,
			 const struct video *vid)
{
	struct selfview_enc *st;
	int err;
	(void)prm;
	(void)vid;

	if (!stp || !ctx || !vf)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	err = selfview_alloc(&st->selfview, ctx);
	if (err)
		goto out;

	if (0 == str_casecmp(vf->name, "selfview_window")) {

		struct list *lst = baresip_vidispl();

		err = vidisp_alloc(&st->disp, lst, NULL, NULL,
				   NULL, NULL, NULL);
		if (err)
			goto out;

		st->vd = vidisp_find(lst, NULL);
		if (!st->vd) {
			err = ENOENT;
			goto out;
		}

		info("selfview: created video display (%s)\n",
		     st->vd->name);
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct vidfilt_enc_st *)st;

	return err;
}


static int decode_update(struct vidfilt_dec_st **stp, void **ctx,
			 const struct vidfilt *vf, struct vidfilt_prm *prm,
			 const struct video *vid)
{
	struct selfview_dec *st;
	int err;
	(void)prm;
	(void)vid;

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
	int err = 0;

	if (!frame)
		return 0;

	if (enc->vd && enc->vd->disph)
		err = enc->vd->disph(enc->disp, "Selfview", frame, *timestamp);

	return err;
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

	mtx_lock(&selfview->lock);
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
	mtx_unlock(&selfview->lock);

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

	mtx_lock(&sv->lock);
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
	mtx_unlock(&sv->lock);

	return 0;
}


static struct vidfilt selfview_win = {
	.name    = "selfview_window",
	.encupdh = encode_update,
	.ench    = encode_win,
};
static struct vidfilt selfview_pip = {
	.name    = "selfview_pip",
	.encupdh = encode_update,
	.ench    = encode_pip,
	.decupdh = decode_update,
	.dech    = decode_pip
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
