/**
 * @file gst_video/sdp.c H.264 SDP Functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "gst_video.h"


static const uint8_t gst_video_h264_level_idc = 0x0c;


uint32_t gst_video_h264_packetization_mode(const char *fmtp)
{
	struct pl pl, mode;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "packetization-mode", &mode))
		return pl_u32(&mode);

	return 0;
}


int gst_video_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		       bool offer, void *arg)
{
	struct vidcodec *vc = arg;
	const uint8_t profile_idc = 0x42; /* baseline profile */
	const uint8_t profile_iop = 0x80;
	(void)offer;

	if (!mb || !fmt || !vc)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s"
			   " packetization-mode=0"
			   ";profile-level-id=%02x%02x%02x"
			   "\r\n",
			   fmt->id, profile_idc, profile_iop,
			   gst_video_h264_level_idc);
}


bool gst_video_fmtp_cmp(const char *fmtp1, const char *fmtp2, void *data)
{
	(void)data;

	return gst_video_h264_packetization_mode(fmtp1) ==
		gst_video_h264_packetization_mode(fmtp2);
}
