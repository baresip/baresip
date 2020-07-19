/**
 * @file avfilter/filter.c	Video filter using libavfilter -- filtering
 *
 * Copyright (C) 2020 Mikhail Kurkov
 */

#include <string.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "avfilter.h"
#include "util.h"


int filter_init(struct avfilter_st *st, char* filter_descr,
		struct vidframe *frame)
{
	char args[512];
	int err = 0;

	if (!str_isset(filter_descr)) {
		st->enabled = false;
		return 0;
	}

	const AVFilter *buffersrc = avfilter_get_by_name("buffer");
	const AVFilter *buffersink = avfilter_get_by_name("buffersink");
	AVFilterInOut *outputs = avfilter_inout_alloc();
	AVFilterInOut *inputs = avfilter_inout_alloc();

	enum AVPixelFormat src_format = vidfmt_to_avpixfmt(frame->fmt);
	enum AVPixelFormat pix_fmts[] = { src_format, AV_PIX_FMT_NONE };

	st->filter_graph = avfilter_graph_alloc();
	st->vframe_in = av_frame_alloc();
	st->vframe_out = av_frame_alloc();
	if (!outputs || !inputs || !st->filter_graph ||
	    !st->vframe_in || !st->vframe_out) {
		err = AVERROR(ENOMEM);
		goto end;
	}

	/* buffer video source */
	snprintf(args, sizeof(args),
		 "video_size=%dx%d:pix_fmt=%d:"
		 "time_base=%d/%d:pixel_aspect=1/1",
		 frame->size.w, frame->size.h, src_format, 1, VIDEO_TIMEBASE);

	err = avfilter_graph_create_filter(
		&st->buffersrc_ctx, buffersrc, "in", args, NULL,
		st->filter_graph);
	if (err < 0) {
		warning("avfilter: cannot create buffer source\n");
		goto end;
	}

	/* buffer video sink: to terminate the filter chain. */
	err = avfilter_graph_create_filter(
		&st->buffersink_ctx, buffersink, "out", NULL, NULL,
		st->filter_graph);
	if (err < 0) {
		warning("avfilter: cannot create buffer sink\n");
		goto end;
	}

	err = av_opt_set_int_list(
		st->buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE,
		AV_OPT_SEARCH_CHILDREN);
	if (err < 0) {
		warning("avfilter: cannot set output pixel format\n");
		goto end;
	}

	outputs->name       = av_strdup("in");
	outputs->filter_ctx = st->buffersrc_ctx;
	outputs->pad_idx    = 0;
	outputs->next       = NULL;

	inputs->name       = av_strdup("out");
	inputs->filter_ctx = st->buffersink_ctx;
	inputs->pad_idx    = 0;
	inputs->next       = NULL;

	err = avfilter_graph_parse_ptr(st->filter_graph, filter_descr,
				       &inputs, &outputs, NULL);
	if (err < 0) {
		warning("avfilter: error parsing filter description: %s\n",
			filter_descr);
		goto end;
	}

	err = avfilter_graph_config(st->filter_graph, NULL);
	if (err < 0) {
		warning("avfilter: filter graph config failed\n");
		goto end;
	}

	st->size    = frame->size;
	st->format  = frame->fmt;
	st->enabled = true;

	info("avfilter: filter graph initialized for %s\n", filter_descr);

 end:
	avfilter_inout_free(&inputs);
	avfilter_inout_free(&outputs);

	return err;
}


void filter_reset(struct avfilter_st* st)
{
	if (!st->enabled)
		return;
	if (st->filter_graph)
		avfilter_graph_free(&st->filter_graph);
	if (st->vframe_in)
		av_frame_free(&st->vframe_in);
	if (st->vframe_out)
		av_frame_free(&st->vframe_out);
	st->enabled = false;
	info("avfilter: filter graph reset\n");
}


bool filter_valid(const struct avfilter_st* st, const struct vidframe *frame)
{
	bool res = !st->enabled ||
		((st->size.h == frame->size.h) &&
		 (st->size.w == frame->size.w) &&
		 (st->format == frame->fmt));
	return res;
}


int filter_encode(struct avfilter_st* st, struct vidframe *frame,
		  uint64_t *timestamp)
{
	unsigned i;
	int err;

	if (!frame)
		return 0;

	if (!st->enabled) {
		return 0;
	}

	/* fill the source frame */
	st->vframe_in->format = vidfmt_to_avpixfmt(frame->fmt);
	st->vframe_in->width  = frame->size.w;
	st->vframe_in->height = frame->size.h;
	st->vframe_in->pts    = *timestamp;

	for (i=0; i<4; i++) {
		st->vframe_in->data[i]     = frame->data[i];
		st->vframe_in->linesize[i] = frame->linesize[i];
	}

	/* push source frame into the filter graph */
	err = av_buffersrc_add_frame_flags(
		st->buffersrc_ctx, st->vframe_in, AV_BUFFERSRC_FLAG_KEEP_REF);
	if (err < 0) {
		warning("avfilter: error while feeding the filtergraph\n");
		goto out;
	}

	/* pull filtered frames from the filtergraph */
	av_frame_unref(st->vframe_out);
	err = av_buffersink_get_frame(st->buffersink_ctx, st->vframe_out);
	if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
		goto out;
	if (err < 0) {
		warning("avfilter: error while getting"
			" filtered frame from the filtergraph\n");
		goto out;
	}

	avframe_ensure_topdown(st->vframe_out);

	/* Copy filtered frame back to the input frame */
	for (i=0; i<4; i++) {
		frame->data[i] = st->vframe_out->data[i];
		frame->linesize[i] = st->vframe_out->linesize[i];
	}
	frame->size.h = st->vframe_out->height;
	frame->size.w = st->vframe_out->width;
	frame->fmt = avpixfmt_to_vidfmt(st->vframe_out->format);

 out:
	return err;
}
