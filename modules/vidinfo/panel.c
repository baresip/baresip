/**
 * @file panel.c  Video-info filter -- panel
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vidinfo.h"


static void rrd_append(struct panel *panel, uint64_t val)
{
	if (!panel)
		return;

	panel->rrdv[panel->rrdc++] = val;
	panel->rrd_sum += val;

	if (panel->rrdc >= panel->rrdsz) {
		panel->rrdc = 0;
		panel->rrd_sum = 0;
	}
}


static int rrd_get_average(struct panel *panel, uint64_t *average)
{
	if (!panel->rrdc)
		return ENOENT;

	*average = panel->rrd_sum / panel->rrdc;

	return 0;
}


static void tmr_handler(void *arg)
{
	struct panel *panel = arg;
	uint64_t now = tmr_jiffies();

	tmr_start(&panel->tmr, 2000, tmr_handler, panel);

	if (panel->ts) {
		panel->fps = 1000.0 * panel->nframes / (now - panel->ts);
	}
	panel->nframes = 0;

	panel->ts = now;
}


static void destructor(void *arg)
{
	struct panel *panel = arg;

	tmr_cancel(&panel->tmr);
	mem_deref(panel->label);
	mem_deref(panel->rrdv);

	if (panel->cr)
		cairo_destroy(panel->cr);
	if (panel->surface)
		cairo_surface_destroy(panel->surface);
}


int panel_alloc(struct panel **panelp, const char *label,
		unsigned yoffs, int width, int height)
{
	struct panel *panel;
	int err;

	if (!panelp || !width || !height)
		return EINVAL;

	if (width <= TEXT_WIDTH) {
		info("vidinfo: width too small (%d < %d)\n",
		     width, (int)TEXT_WIDTH );
		return EINVAL;
	}

	panel = mem_zalloc(sizeof(*panel), destructor);
	if (!panel)
		return ENOMEM;

	err = str_dup(&panel->label, label);
	if (err)
		goto out;

	panel->size.w = width;
	panel->size.h = height;
	panel->yoffs = yoffs;
	panel->xoffs = TEXT_WIDTH;

	panel->size_text.w = TEXT_WIDTH;
	panel->size_text.h = height;

	panel->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
						    panel->size_text.w,
						    panel->size_text.h);
	panel->cr = cairo_create(panel->surface);
	if (!panel->surface || !panel->cr) {
		warning("vidinfo: cairo error\n");
		return ENOMEM;
	}

	cairo_select_font_face (panel->cr, "Hyperfont",
				CAIRO_FONT_SLANT_NORMAL,
				CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (panel->cr, height-2);

	panel->rrdc  = 0;
	panel->rrdsz = (width - TEXT_WIDTH) / 2;
	panel->rrdv  = mem_reallocarray(NULL, panel->rrdsz,
					sizeof(*panel->rrdv), NULL);
	if (!panel->rrdv) {
		err = ENOMEM;
		goto out;
	}

	tmr_start(&panel->tmr, 0, tmr_handler, panel);

	info("new panel '%s' (%u x %u) with RRD size %u\n",
	     label, width, height, panel->rrdsz);

 out:
	if (err)
		mem_deref(panel);
	else
		*panelp = panel;

	return err;
}


static void overlay(struct vidframe *dst, unsigned yoffs, struct vidframe *src)
{
	uint8_t *pdst, *psrc;
	unsigned x, y;

	pdst = dst->data[0] + yoffs * dst->linesize[0];
	psrc = src->data[0];

	for (y=0; y<src->size.h; y++) {

		for (x=0; x<src->size.w; x++) {

			/* copy the luma component if visible */
			if (psrc[x] > 16)
				pdst[x] = psrc[x];
		}

		pdst += dst->linesize[0];
		psrc += src->linesize[0];
	}
}


static int draw_text(struct panel *panel, struct vidframe *frame)
{
	char buf[256];
	int width = panel->size_text.w;
	int height = panel->size_text.h;
	struct vidframe f;
	struct vidframe *f2 = NULL;
	cairo_t *cr = panel->cr;
	double tx, ty;
	int err;

	tx = 1;
	ty = height - 3;

	/* draw background */
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
	cairo_fill (cr);

	/* Draw text */
	if (re_snprintf(buf, sizeof(buf), "%s %2.2f fps",
			panel->label, panel->fps) < 0)
		return ENOMEM;

	cairo_move_to (cr, tx, ty);
	cairo_text_path (cr, buf);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_fill_preserve (cr);
	cairo_set_line_width (cr, 0.6);
	cairo_stroke (cr);

	vidframe_init_buf(&f, VID_FMT_RGB32, &panel->size_text,
			  cairo_image_surface_get_data(panel->surface));

	err = vidframe_alloc(&f2, frame->fmt, &panel->size_text);
	if (err)
		goto out;

	vidconv(f2, &f, 0);

	overlay(frame, panel->yoffs, f2);

 out:
	mem_deref(f2);
	return err;
}


static void dim_frame(struct vidframe *frame, unsigned yoffs, unsigned height)
{
	unsigned x, y;
	uint8_t *p;
	bool lower = (yoffs > 0);
	double grade = lower ? 1.00 : (1.00 - PANEL_HEIGHT/100.0);

	p = frame->data[0] + yoffs * frame->linesize[0];

	/* first dim the background */
	for (y = 0; y < height; y++) {

		for (x = 0; x < frame->size.w; x++) {
			p[x] = p[x] * grade;
		}

		p += frame->linesize[0];

		if (lower)
			grade -= 0.01;
		else
			grade += 0.01;
	}
}


static void draw_graph(struct panel *panel, struct vidframe *frame)
{
	uint64_t avg;
	unsigned y0 = panel->yoffs;
	size_t i;

	if (rrd_get_average(panel, &avg))
		return;

	for (i=0; i<panel->rrdc; i++) {

		uint64_t value;
		double ratio;
		unsigned pixels;
		unsigned x = panel->xoffs + (unsigned)i * 2;
		unsigned y;
		value = panel->rrdv[i];

		ratio = (double)value / (double)avg;

		pixels = (unsigned)((double)panel->size.h * ratio * 0.5f);

		pixels = min(pixels, panel->size.h);

		y = y0 + panel->size.h - pixels;

		vidframe_draw_vline(frame, x, y, pixels, 220, 220, 220);
	}
}


int panel_draw(struct panel *panel, struct vidframe *frame)
{
	int err;

	if (!panel || !frame)
		return EINVAL;

	dim_frame(frame, panel->yoffs, panel->size.h);

	err = draw_text(panel, frame);
	if (err)
		return err;
	draw_graph(panel, frame);

	return 0;
}


void panel_add_frame(struct panel *panel, uint64_t pts)
{
	if (!panel)
		return;

	if (panel->pts_prev) {
		rrd_append(panel, pts - panel->pts_prev);
	}

	panel->nframes++;
	panel->pts_prev = pts;
}
