/* @file rtspd.h
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _RTSPD_H_
#define _RTSPD_H_

#define DEFAULT_RTSP_PORT 554
#define SESSBYTES (25 + 1)

extern struct rtsp_sock *rtspsock;

enum stream_type {
	STREAMT_AUDIO,
	STREAMT_VIDEO,
	STREAMT_ABACK,

	STREAMT_MAX,
};

// RTSP Session struct
struct rtsp_session {
	struct le le;
	struct list rtsp_stream_l;
	char session [SESSBYTES];
	struct tmr timer;
	uint32_t timeout;
};

struct rtsp_stream {
	struct le le;

	struct sa tar;
	struct onvif_filter_stream *fs;
	struct onvif_fakevideo_stream *fvs;
	int proto;
	enum stream_type type;
	uint16_t rtp_port;
	uint16_t rtcp_port;
};

enum resource {
	RESOURCE_AUDIO,
	RESOURCE_VIDEO,
	RESOURCE_AUDIBACK,

	RESOURCE_MAX
};

void rtsp_msg_handler(struct rtsp_conn *conn, const struct rtsp_msg *msg,
	void *arg);

void rtsp_init(void);
void rtsp_session_deinit(void);

#endif /* _RTSPD_H_ */

