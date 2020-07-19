/**
 * @file util.c	 Video filter using libavfilter -- utility functions
 *
 * Copyright (C) 2020 Mikhail Kurkov
 */

#include <stdlib.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavutil/frame.h>
#include "util.h"


static int swap_lines(uint8_t *a, uint8_t *b, uint8_t *tmp, size_t size)
{
	memcpy(tmp, a, size);
	memcpy(a, b, size);
	memcpy(b, tmp, size);
	return 0;
}


static int reverse_lines(uint8_t *data, int linesize, int count)
{
	size_t size = abs(linesize) * sizeof(uint8_t);
	uint8_t *tmp = malloc(size);
	if (!tmp)
		return ENOMEM;

	for (int i = 0; i < count/2; i++)
		swap_lines(data + linesize * i,
			   data + linesize * (count - i - 1),
			   tmp,
			   size);

	free(tmp);
	return 0;
}


/*
 * Sometimes AVFrame contains planes with lines in bottom-up order.
 * Then linesize is negative and data points to the last row in buffer.
 * Baresip uses unsigned linesizes, lets reorder lines to fix it.
 */
int avframe_ensure_topdown(AVFrame *frame)
{
	switch (frame->format) {

	case AV_PIX_FMT_YUV420P:
		for (int i=0; i<4; i++) {
			int ls = frame->linesize[i];
			if (ls >= 0) continue;
			int h = i == 0 ? frame->height : frame->height/2;
			reverse_lines(frame->data[i], ls, h);
			frame->data[i]     = frame->data[i] + ls * (h - 1);
			frame->linesize[i] = abs(ls);
		}
		break;

	default:
		/* TODO support more formats */
		for (int i=0; i<4; i++) {

			if (frame->linesize[0] <0) {

				warning("avfilter: unsupported frame"
					" format with negative linesize: %d",
					frame->format);

				return EPROTO;
			}
		}
	}

	return 0;
}


enum AVPixelFormat vidfmt_to_avpixfmt(enum vidfmt fmt)
{

	switch (fmt) {

	case VID_FMT_YUV420P: return AV_PIX_FMT_YUV420P;
	case VID_FMT_YUV444P: return AV_PIX_FMT_YUV444P;
	case VID_FMT_NV12:    return AV_PIX_FMT_NV12;
	case VID_FMT_NV21:    return AV_PIX_FMT_NV21;
	default:              return AV_PIX_FMT_NONE;
	}
}


enum vidfmt avpixfmt_to_vidfmt(enum AVPixelFormat pix_fmt)
{
	switch (pix_fmt) {

	case AV_PIX_FMT_YUV420P:  return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUVJ420P: return VID_FMT_YUV420P;
	case AV_PIX_FMT_YUV444P:  return VID_FMT_YUV444P;
	case AV_PIX_FMT_NV12:     return VID_FMT_NV12;
	case AV_PIX_FMT_NV21:     return VID_FMT_NV21;
	default:                  return (enum vidfmt)-1;
	}
}
