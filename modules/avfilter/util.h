/**
 * @file util.h  Video filter using libavfilter -- utility functions
 *
 * Copyright (C) 2020 Mikhail Kurkov
 */


int avframe_ensure_topdown(AVFrame *frame);
enum AVPixelFormat vidfmt_to_avpixfmt(enum vidfmt fmt);
enum vidfmt avpixfmt_to_vidfmt(enum AVPixelFormat pix_fmt);
