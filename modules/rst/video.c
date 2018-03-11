/**
 * @file rst/video.c MP3/ICY HTTP Video Source
 *
 * Copyright (C) 2011 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <pthread.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <cairo/cairo.h>
#include "rst.h"


struct vidsrc_st {
	const struct vidsrc *vs;  /* pointer to base-class (inheritance) */
	pthread_mutex_t mutex;
	pthread_t thread;
	struct vidsrc_prm prm;
	struct vidsz size;
	struct rst *rst;
	cairo_surface_t *surface;
	cairo_t *cairo;
	struct vidframe *frame;
	vidsrc_frame_h *frameh;
	void *arg;
	bool run;
};


static struct vidsrc *vidsrc;


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	rst_set_video(st->rst, NULL);
	mem_deref(st->rst);

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->cairo)
		cairo_destroy(st->cairo);

	if (st->surface)
		cairo_surface_destroy(st->surface);

	mem_deref(st->frame);
}


static void *video_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct vidsrc_st *st = arg;

	while (st->run) {

		uint64_t timestamp;

		sys_msleep(4);

		now = tmr_jiffies();

		if (ts > now)
			continue;

		timestamp = ts * VIDEO_TIMEBASE / 1000;

		pthread_mutex_lock(&st->mutex);
		st->frameh(st->frame, timestamp, st->arg);
		pthread_mutex_unlock(&st->mutex);

		ts += 1000/st->prm.fps;
	}

	return NULL;
}


static void background(cairo_t *cr, unsigned width, unsigned height)
{
	cairo_pattern_t *pat;
	double r, g, b;

	pat = cairo_pattern_create_linear(0.0, 0.0,  0.0, height);
	if (!pat)
		return;

	r = 0.0;
	g = 0.0;
	b = 0.8;

	cairo_pattern_add_color_stop_rgba(pat, 1, r, g, b, 1);
	cairo_pattern_add_color_stop_rgba(pat, 0, 0, 0, 0.2, 1);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_set_source(cr, pat);
	cairo_fill(cr);

	cairo_pattern_destroy(pat);
}


static void icy_printf(cairo_t *cr, int x, int y, double size,
		       const char *fmt, ...)
{
	char buf[4096] = "";
	va_list ap;

	va_start(ap, fmt);
	(void)re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/* Draw text */
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, size);
	cairo_move_to(cr, x, y);
	cairo_text_path(cr, buf);
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_fill(cr);
}


static size_t linelen(const struct pl *pl)
{
	size_t len = 72, i;

	if (pl->l <= len)
		return pl->l;

	for (i=len; i>1; i--) {

		if (pl->p[i-1] == ' ') {
			len = i;
			break;
		}
	}

	return len;
}


void rst_video_update(struct vidsrc_st *st, const char *name, const char *meta)
{
	struct vidframe frame;

	if (!st)
		return;

	background(st->cairo, st->size.w, st->size.h);

	icy_printf(st->cairo, 50, 100, 40.0, "%s", name);

	if (meta) {

		struct pl title;

		if (!re_regex(meta, strlen(meta),
			      "StreamTitle='[ \t]*[^;]+;", NULL, &title)) {

			unsigned i;

			title.l--;

			for (i=0; title.l; i++) {

				const size_t len = linelen(&title);

				icy_printf(st->cairo, 50, 150 + 25*i, 18.0,
					   "%b", title.p, len);

				title.p += len;
				title.l -= len;
			}
		}
	}

	vidframe_init_buf(&frame, VID_FMT_RGB32, &st->size,
			  cairo_image_surface_get_data(st->surface));

	pthread_mutex_lock(&st->mutex);
	vidconv(st->frame, &frame, NULL);
	pthread_mutex_unlock(&st->mutex);
}


static int alloc_handler(struct vidsrc_st **stp, const struct vidsrc *vs,
			 struct media_ctx **ctx, struct vidsrc_prm *prm,
			 const struct vidsz *size, const char *fmt,
			 const char *dev, vidsrc_frame_h *frameh,
			 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err;

	(void)fmt;
	(void)errorh;

	if (!stp || !vs || !prm || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	err = pthread_mutex_init(&st->mutex, NULL);
	if (err)
		goto out;

	st->vs     = vs;
	st->prm    = *prm;
	st->size   = *size;
	st->frameh = frameh;
	st->arg    = arg;

	st->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						 size->w, size->h);
	if (!st->surface) {
		err = ENOMEM;
		goto out;
	}

	st->cairo = cairo_create(st->surface);
	if (!st->cairo) {
		err = ENOMEM;
		goto out;
	}

	err = vidframe_alloc(&st->frame, VID_FMT_YUV420P, size);
	if (err)
		goto out;

	vidframe_fill(st->frame, 0, 0, 0);

	if (ctx && *ctx && (*ctx)->id && !strcmp((*ctx)->id, "rst")) {
		st->rst = mem_ref(*ctx);
	}
	else {
		err = rst_alloc(&st->rst, dev);
		if (err)
			goto out;

		if (ctx)
			*ctx = (struct media_ctx *)st->rst;
	}

	rst_set_video(st->rst, st);

	st->run = true;

	err = pthread_create(&st->thread, NULL, video_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


int rst_video_init(void)
{
	return vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "rst", alloc_handler, NULL);
}


void rst_video_close(void)
{
	vidsrc = mem_deref(vidsrc);
}
