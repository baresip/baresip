/**
 * @file cairo.c  Cairo module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _BSD_SOURCE 1
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <cairo/cairo.h>


#if !defined (M_PI)
#define M_PI 3.14159265358979323846264338327
#endif


/*
 * Note: This module is very experimental!
 *
 * Use Cairo library to draw graphics into a frame buffer
 */

struct vidsrc_st {
	struct vidsrc *vs;  /* inheritance */

	struct vidsrc_prm prm;
	struct vidsz size;
	cairo_surface_t *surface;
	cairo_t *cr;
	double step;
	bool run;
	pthread_t thread;
	vidsrc_frame_h *frameh;
	void *arg;
};


static struct vidsrc *vidsrc;


static void destructor(void *arg)
{
	struct vidsrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->cr)
		cairo_destroy(st->cr);
	if (st->surface)
		cairo_surface_destroy(st->surface);

	mem_deref(st->vs);
}


static void draw_gradient(cairo_t *cr, double step, int width, int height)
{
	cairo_pattern_t *pat;
	double r, g, b;
	double x, y, tx, ty;
	char buf[128];
	double fontsize = 20.0;

	r = 0.1 + fabs(sin(5 * step));
	g = 0.0;
	b = 0.1 + fabs(sin(3 * step));

	x = width * (sin(10 * step) + 1)/2;
	y = height * (1 - fabs(sin(30 * step)));

	tx = width/2 * (sin(5 * step) + 1)/2;
	ty = fontsize + (height - fontsize) * (1 - fabs(sin(20 * step)));


	pat = cairo_pattern_create_linear (0.0, 0.0,  0.0, height);
	cairo_pattern_add_color_stop_rgba (pat, 1, r, g, b, 1);
	cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0, 1);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_set_source (cr, pat);
	cairo_fill (cr);
	cairo_pattern_destroy (pat);

	pat = cairo_pattern_create_radial (x-128, y-128, 25.6,
					   x+128, y+128, 128.0);
	cairo_pattern_add_color_stop_rgba (pat, 0, 0, 1, 0, 1);
	cairo_pattern_add_color_stop_rgba (pat, 1, 0, 0, 0, 1);
	cairo_set_source (cr, pat);
	cairo_arc (cr, x, y, 76.8, 0, 2 * M_PI);
	cairo_fill (cr);
	cairo_pattern_destroy (pat);

	/* Draw text */
	cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
				CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, fontsize);

	re_snprintf(buf, sizeof(buf), "%H", fmt_gmtime, NULL);

	cairo_move_to (cr, tx, ty);
	cairo_text_path (cr, buf);
	cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_fill_preserve (cr);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_set_line_width (cr, 0.1);
	cairo_stroke (cr);
}


static void process(struct vidsrc_st *st)
{
	struct vidframe f;

	draw_gradient(st->cr, st->step, st->size.w, st->size.h);
	st->step += 0.02 / st->prm.fps;

	vidframe_init_buf(&f, VID_FMT_RGB32, &st->size,
			  cairo_image_surface_get_data(st->surface));

	st->frameh(&f, st->arg);
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;
	uint64_t ts = 0;

	while (st->run) {

		uint64_t now;

		sys_msleep(2);

		now = tmr_jiffies();
		if (!ts)
			ts = now;

		if (ts > now)
			continue;

		process(st);

		ts += 1000/st->prm.fps;
	}

	return NULL;
}


static int alloc(struct vidsrc_st **stp, struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct vidsrc_st *st;
	int err = 0;

	(void)ctx;
	(void)fmt;
	(void)dev;
	(void)errorh;

	if (!stp || !prm || !size || !frameh)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = mem_ref(vs);
	st->frameh = frameh;
	st->arg    = arg;
	st->prm    = *prm;
	st->size   = *size;

	st->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						 size->w, size->h);
	st->cr = cairo_create(st->surface);

	info("cairo: surface with format %d (%d x %d) stride=%d\n",
	     cairo_image_surface_get_format(st->surface),
	     cairo_image_surface_get_width(st->surface),
	     cairo_image_surface_get_height(st->surface),
	     cairo_image_surface_get_stride(st->surface));

	st->step = rand_u16() / 1000.0;

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
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


static int module_init(void)
{
	return vidsrc_register(&vidsrc, "cairo", alloc, NULL);
}


static int module_close(void)
{
	vidsrc = mem_deref(vidsrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(cairo) = {
	"cairo",
	"vidsrc",
	module_init,
	module_close
};
