/**
 * @file cairo.c  Cairo module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
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


/**
 * @defgroup cairo cairo
 *
 * Cairo video-source module is a video generator for testing
 * and demo purposes.
 *
 * Note: This module is very experimental!
 *
 * Use Cairo library to draw graphics into a frame buffer
 */


enum {
	FONT_SIZE = 18
};

struct vidsrc_st {
	const struct vidsrc *vs;  /* inheritance */

	struct vidsrc_prm prm;
	struct vidsz size;
	cairo_surface_t *surface;
	cairo_t *cr;
	cairo_surface_t *surface_logo;
	cairo_t *cr_logo;
	double logo_width;
	double logo_height;
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

	if (st->cr_logo)
		cairo_destroy(st->cr_logo);
	if (st->surface_logo)
		cairo_surface_destroy(st->surface_logo);
}


static void draw_background(cairo_t *cr, double color_step,
			    int width, int height)
{
	cairo_pattern_t *pat;
	double grey, r, g, b;

	grey = 0.1 + fabs(sin(3 * color_step));
	r = grey;
	g = grey;
	b = grey;

	pat = cairo_pattern_create_linear (0.0, 0.0,  0.0, height);
	cairo_pattern_add_color_stop_rgba (pat, 1, r, g, b, 1);
	cairo_pattern_add_color_stop_rgba (pat, 0, 0, 0, 0, 1);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_set_source (cr, pat);
	cairo_fill (cr);
	cairo_pattern_destroy (pat);
}


static void draw_text(struct vidsrc_st *st, int x, int y,
		      const char *fmt, ...)
{
	char buf[4096] = "";
	va_list ap;

	va_start(ap, fmt);
	(void)re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	cairo_set_source_rgb(st->cr, 1.0, 1.0, 1.0);  /* white */

	cairo_set_font_size(st->cr, FONT_SIZE);
	cairo_move_to(st->cr, x, y);
	cairo_show_text(st->cr, buf);
}


static void draw_logo(struct vidsrc_st *st)
{
	double x, y;

	x = (st->size.w - st->logo_width) * (sin(10 * st->step) + 1)/2;
	y = (st->size.h - st->logo_height)* (1 - fabs(sin(30 * st->step)));

	cairo_set_source_surface(st->cr, st->surface_logo, x, y);
	cairo_paint(st->cr);
}


static void process(struct vidsrc_st *st, uint64_t timestamp)
{
	struct vidframe f;
	unsigned xoffs = 2, yoffs = 24;

	draw_background(st->cr, st->step, st->size.w, st->size.h);

	draw_text(st, xoffs, yoffs + FONT_SIZE, "%H", fmt_gmtime, NULL);

	draw_text(st, xoffs, yoffs + FONT_SIZE*2, "%u x %u @ %.2f fps",
		  st->size.w, st->size.h, st->prm.fps);

	draw_text(st, xoffs, yoffs + FONT_SIZE*3, "Time: %.3f sec",
		  timestamp / (double)VIDEO_TIMEBASE);

	draw_logo(st);

	st->step += 0.02 / st->prm.fps;

	vidframe_init_buf(&f, VID_FMT_RGB32, &st->size,
			  cairo_image_surface_get_data(st->surface));

	st->frameh(&f, timestamp, st->arg);
}


static void *read_thread(void *arg)
{
	struct vidsrc_st *st = arg;
	uint64_t ts = 0, ts_start = 0;

	while (st->run) {

		uint64_t now;
		uint64_t timestamp;

		sys_msleep(2);

		now = tmr_jiffies();
		if (!ts) {
			ts = ts_start = now;
		}

		if (ts > now)
			continue;

		timestamp = (ts - ts_start) * VIDEO_TIMEBASE / 1000;

		process(st, timestamp);

		ts += 1000/st->prm.fps;
	}

	return NULL;
}


static int load_logo(struct vidsrc_st *st, const char *filename)
{
	cairo_surface_t *logo;
	double lw;
	double scale;
	int err = 0;

	logo = cairo_image_surface_create_from_png(filename);
	if (!logo) {
		warning("cairo: failed to load PNG logo\n");
		err = ENOENT;
		goto out;
	}

	if (!cairo_image_surface_get_width(logo) ||
	    !cairo_image_surface_get_height(logo)) {
		warning("cairo: invalid logo (%s)\n", filename);
		err = ENOENT;
		goto out;
	}

	st->logo_width = st->size.w / 2;
	lw = cairo_image_surface_get_width(logo);
	scale = (double)st->logo_width / (double)lw;

	st->logo_height = cairo_image_surface_get_height(logo) * scale;

	/* create a scaled-down logo with same aspect ratio */

	st->surface_logo = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						      st->logo_width,
						      st->logo_height);
	if (!st->surface_logo) {
		err = ENOMEM;
		goto out;
	}

	st->cr_logo = cairo_create(st->surface_logo);
	if (!st->cr_logo) {
		err = ENOMEM;
		goto out;
	}

	cairo_scale(st->cr_logo, scale, scale);

	cairo_set_source_surface(st->cr_logo, logo, 0, 0);
	cairo_paint(st->cr_logo);

	info("cairo: scaling logo '%s' from %d x %d to %.1f x %.1f\n",
	     filename,
	     cairo_image_surface_get_width(logo),
	     cairo_image_surface_get_height(logo),
	     st->logo_width,
	     st->logo_height);

 out:
	if (logo)
		cairo_surface_destroy(logo);
	return err;
}


static int alloc(struct vidsrc_st **stp, const struct vidsrc *vs,
		 struct media_ctx **ctx, struct vidsrc_prm *prm,
		 const struct vidsz *size, const char *fmt,
		 const char *dev, vidsrc_frame_h *frameh,
		 vidsrc_error_h *errorh, void *arg)
{
	struct config *cfg;
	struct vidsrc_st *st;
	char logo[256];
	int err = 0;

	(void)ctx;
	(void)fmt;
	(void)dev;
	(void)errorh;

	if (!stp || !prm || !size || !frameh)
		return EINVAL;

	cfg = conf_config();
	if (!cfg)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->vs     = vs;
	st->frameh = frameh;
	st->arg    = arg;
	st->prm    = *prm;
	st->size   = *size;

	st->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						 size->w, size->h);
	if (!st->surface) {
		err = ENOMEM;
		goto out;
	}

	st->cr = cairo_create(st->surface);
	if (!st->cr) {
		err = ENOMEM;
		goto out;
	}

	cairo_select_font_face(st->cr, "Sans",
				CAIRO_FONT_SLANT_NORMAL,
				CAIRO_FONT_WEIGHT_BOLD);

	info("cairo: surface with format %d (%d x %d) stride=%d\n",
	     cairo_image_surface_get_format(st->surface),
	     cairo_image_surface_get_width(st->surface),
	     cairo_image_surface_get_height(st->surface),
	     cairo_image_surface_get_stride(st->surface));

	st->step = rand_u16() / 1000.0;

	re_snprintf(logo, sizeof(logo), "%s/logo.png", cfg->audio.audio_path);

	err = load_logo(st, logo);
	if (err) {
		goto out;
	}

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
	return vidsrc_register(&vidsrc, baresip_vidsrcl(),
			       "cairo", alloc, NULL);
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
