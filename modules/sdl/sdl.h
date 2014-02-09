/**
 * @file sdl.h  Simple DirectMedia Layer module -- internal interface
 *
 * Copyright (C) 2010 Creytiv.com
 */


void picture_copy(uint8_t *data[4], uint16_t linesize[4],
		  const struct vidframe *frame);
