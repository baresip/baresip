/**
 * @file sdp.c  Commend specific video source
 *
 * Copyright (C) 2020 Commend.com
 */

#include <re.h>
#include <baresip.h>
#include "comvideo.h"


static const uint8_t h264_level_idc = 0x1F;


int comvideo_fmtp_enc(
	struct mbuf *mb, const struct sdp_format *fmt, bool offer, void *arg)
{
	struct vidcodec *vc = arg;
	const uint8_t profile_idc = 0x42; /* baseline profile */
	const uint8_t profile_iop = 0xe0;
	(void)offer;

	if (!mb || !fmt || !vc)
		return 0;

	return mbuf_printf(
		mb, "a=fmtp:%s "
		";profile-level-id=%02x%02x%02x\r\n",
		fmt->id,
		profile_idc, profile_iop, h264_level_idc);
}
