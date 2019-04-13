/**
 * @file avcodec/sdp.c  Video codecs using libavcodec -- SDP functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include "avcodec.h"


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


bool h264_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg)
{
	const struct vidcodec *vc = arg;
	(void)lfmtp;

	if (!vc)
		return false;

	return h264_packetization_mode(vc->variant) ==
		h264_packetization_mode(rfmtp);
}
