/**
 * @file draw.c  Video draw functions
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vidinfo.h"
#include "xga_font_data.h"


static void dim_region(struct vidframe *frame,
		       int x0, int y0, unsigned width, unsigned height)
{
	unsigned x, y;
	uint8_t *p;
	double grade = 0.5;

	p = frame->data[0] + x0 + y0 * frame->linesize[0];

	/* first dim the background */
	for (y = 0; y < height; y++) {

		for (x = 0; x < width; x++) {
			p[x] = p[x] * grade;
		}

		p += frame->linesize[0];
	}
}


static void draw_char(struct vidframe *frame, int x0, int y0, uint8_t ch)
{
	const uint8_t *font;
	int x, y;

	font = &vidinfo_cga_font[ch * FONT_HEIGHT];

	for (y = 0; y < FONT_HEIGHT; y++) {

		/* draw one raster-line */
		for (x = 0; x < FONT_WIDTH; x++) {

			if (*font & 1<<(7-x)) {

				vidframe_draw_point(frame,
						    x0+x, y0+y,
						    255, 255, 255);
			}
		}

		++font;
	}
}


static void draw_text(struct vidframe *frame, struct vidpt *pos,
		      const char *fmt, ...)
{
	char buf[4096] = "";
	va_list ap;
	int len, i;
	const unsigned x0 = pos->x;

	va_start(ap, fmt);
	len = re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	for (i=0; i<len; i++) {

		int ch = buf[i];

		if (ch == '\n') {
			pos->x = x0;
			pos->y += FONT_HEIGHT;
			continue;
		}

		draw_char(frame, pos->x, pos->y, ch);

		pos->x += FONT_WIDTH;
	}
}


int vidinfo_draw_box(struct vidframe *frame, uint64_t timestamp,
		     const struct stats *stats, const struct video *vid,
		     int x0, int y0, int width, int height)
{
	const struct vidcodec *vc;
	struct stream *strm;
	const struct rtcp_stats *rtcp;
	struct vidpt pos = {x0+2, y0+2};
	int64_t dur;

	dur = timestamp - stats->last_timestamp;

	dim_region(frame, x0, y0, width, height);

	vidframe_draw_rect(frame, x0, y0, width, height, 255, 255, 255);
	vidframe_draw_rect(frame, x0+1, y0+1, width, height, 0, 0, 0);

	draw_text(frame, &pos,
		  "[%H]\n"
		  "Resolution:   %u x %u\n"
		  "Framerate:    %.1f\n"
		  ,
		  fmt_gmtime, NULL,
		  frame->size.w, frame->size.h,
		  (double)VIDEO_TIMEBASE / (double)dur);

	vc = video_codec(vid, false);
	if (vc) {
		draw_text(frame, &pos, "Decoder:      %s\n", vc->name);
	}

	strm = video_strm(vid);
	rtcp = stream_rtcp_stats(strm);
	if (rtcp && rtcp->rx.sent) {

		double loss;

		loss = 100.0 * (double)rtcp->rx.lost / (double)rtcp->rx.sent;

		draw_text(frame, &pos,
			  "Jitter:       %.1f ms\n"
			  "Packetloss:   %.2f %%\n"
			  ,
			  (double)rtcp->rx.jit * .001, loss);
	}

	return 0;
}
