/**
 * @file avcodec/sdp.c  Video codecs using libavcodec -- SDP functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include "avcodec.h"


static const uint8_t h264_level_idc = 0x1f;


uint32_t h264_packetization_mode(const char *fmtp)
{
	struct pl pl, mode;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "packetization-mode", &mode))
		return pl_u32(&mode);

	return 0;
}


int avcodec_h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			  bool offer, void *arg)
{
	struct vidcodec *vc = arg;
	const uint8_t profile_idc = 0x42; /* baseline profile */
	const uint8_t profile_iop = 0xe0;
	(void)offer;

	if (!mb || !fmt || !vc)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s"
			   " %s"
			   ";profile-level-id=%02x%02x%02x"
			   "\r\n",
			   fmt->id, vc->variant,
			   profile_idc, profile_iop, h264_level_idc);
}


bool avcodec_h264_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg)
{
	const struct vidcodec *vc = arg;
	(void)lfmtp;

	if (!vc)
		return false;

	return h264_packetization_mode(vc->variant) ==
		h264_packetization_mode(rfmtp);
}
