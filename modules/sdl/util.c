/**
 * @file util.c  Simple DirectMedia Layer module -- utilities
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "sdl.h"


static void img_copy_plane(uint8_t *dst, int dst_wrap,
			   const uint8_t *src, int src_wrap,
			   int width, int height)
{
	if (!dst || !src)
		return;

	for (;height > 0; height--) {
		memcpy(dst, src, width);
		dst += dst_wrap;
		src += src_wrap;
	}
}


static int get_plane_bytewidth(int width, int plane)
{
	if (plane == 1 || plane == 2)
		width = -((-width) >> 1);

	return (width * 8 + 7) >> 3;
}


void picture_copy(uint8_t *data[4], uint16_t linesize[4],
		  const struct vidframe *frame)
{
	const int map[3] = {0, 2, 1};
	int i;

	for (i=0; i<3; i++) {
		int h;
		int bwidth = get_plane_bytewidth(frame->size.w, i);
		h = frame->size.h;
		if (i == 1 || i == 2) {
			h = -((-frame->size.h) >> 1);
		}
		img_copy_plane(data[map[i]], linesize[map[i]],
			       frame->data[i], frame->linesize[i],
			       bwidth, h);
	}
}
