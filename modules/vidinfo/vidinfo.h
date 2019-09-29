/**
 * @file vidinfo.h  Video-info filter
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */


struct stats {
	uint64_t last_timestamp;
};


int vidinfo_draw_box(struct vidframe *frame, uint64_t timestamp,
		     const struct stats *stats, const struct video *vid,
		     int x0, int y0, int width, int height);
