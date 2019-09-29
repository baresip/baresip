/**
 * @file xga_font_data.c  CGA Font -- interface
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */


enum {
	FONT_WIDTH  = 8,
	FONT_HEIGHT = 8,
};

extern const uint8_t vidinfo_cga_font[256 * FONT_HEIGHT];
