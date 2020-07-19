/**
 * @file avfilter.h  Video filter using libavfilter -- internal API
 *
 * Copyright (C) 2020 Mikhail Kurkov
 */

/* Maximum filter length */
#define MAX_DESCR 512


/* Filter state */
struct avfilter_st {
	struct vidfilt_enc_st vf;   /* base class */

	struct vidsz size;
	enum vidfmt format;
	bool enabled;

	AVFilterContext *buffersink_ctx;
	AVFilterContext *buffersrc_ctx;
	AVFilterGraph *filter_graph;
	AVFrame *vframe_in;
	AVFrame *vframe_out;
};


/*
 * Filter API
 */

int filter_init(struct avfilter_st *st, char* filter_descr,
		struct vidframe *frame);

void filter_reset(struct avfilter_st *st);

bool filter_valid(const struct avfilter_st *st, const struct vidframe *frame);

int filter_encode(struct avfilter_st *st, struct vidframe *frame,
		  uint64_t *timestamp);
