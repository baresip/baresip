/* @file filter.h
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _ONVIF_AU_FILTER_H_
#define _ONVIF_AU_FILTER_H_

#include <re.h>
#include <rem.h>
#include <baresip.h>

struct onvif_filter_stream;

void onvif_aufilter_rtsp_wrapper(struct mbuf *mb, void *arg);


int onvif_aufilter_stream_alloc(struct onvif_filter_stream **fssp,
	uint32_t srate, uint8_t ch, const char *codec);


int onvif_aufilter_audio_recv_start(struct onvif_filter_stream *fs,
	struct sa *sa, int proto);
void onvif_aufilter_audio_recv_stop(struct onvif_filter_stream *fs);


int onvif_aufilter_audio_send_start(struct onvif_filter_stream *fs,
	const struct sa *sa, const struct rtsp_conn *conn, int proto);
void onvif_aufilter_audio_send_stop(struct onvif_filter_stream *fs);


void onvif_set_aufilter_src_en(bool a);
void onvif_set_aufilter_play_en(bool a);


void register_onvif_filter(void);
void unregister_onvif_filter(void);

#endif /* ONVIF AU-FILTER */
