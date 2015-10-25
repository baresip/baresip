/**
 * @file vidinfo.h  Video-info filter
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */


#include <cairo/cairo.h>


#define PANEL_HEIGHT 24
#define TEXT_WIDTH 220


struct panel {
	struct vidsz size;
	struct vidsz size_text;
	unsigned yoffs;
	unsigned xoffs;
	char *label;

	uint64_t *rrdv;
	size_t rrdsz;
	size_t rrdc;
	uint64_t rrd_sum;

	unsigned nframes;
	uint64_t ts;
	double fps;
	struct tmr tmr;

	uint64_t pts_prev;

	/* cairo backend: */
	cairo_surface_t *surface;
	cairo_t *cr;
};

int  panel_alloc(struct panel **panelp, const char *label,
		 unsigned yoffs, int width, int height);
void panel_add_frame(struct panel *panel, uint64_t pts);
int  panel_draw(struct panel *panel, struct vidframe *frame);
