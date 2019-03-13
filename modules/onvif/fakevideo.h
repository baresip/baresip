/* @file fakevideo.h
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _FAKEVIDEO_H_
#define _FAKEVIDEO_H_

#include "filter.h"
#include "rtspd.h"

struct lock *onvif_fvlock;
struct onvif_fakevideo_stream;

int onvif_fakevideo_alloc(struct onvif_fakevideo_stream **fvsp,
	const char *codec);
int onvif_fakevideo_start(struct onvif_fakevideo_stream *fvs, int proto,
	const struct sa *tar, const struct rtsp_conn *conn);
void onvif_fakevideo_stop(struct onvif_fakevideo_stream *fvs);

#endif /* _FAKEVIDEO_H_ */
