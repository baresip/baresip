/**
 * @file g7221/sdp.c G.722.1 SDP Functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "g7221.h"


static uint32_t g7221_bitrate(const char *fmtp)
{
	struct pl pl, bitrate;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "bitrate", &bitrate))
		return pl_u32(&bitrate);

	return 0;
}


int g7221_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		  bool offer, void *arg)
{
	const struct g7221_aucodec *g7221 = arg;
	(void)offer;

	if (!mb || !fmt || !g7221)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s bitrate=%u\r\n",
			   fmt->id, g7221->bitrate);
}


bool g7221_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg)
{
	const struct g7221_aucodec *g7221 = arg;
	(void)lfmtp;

	if (!g7221)
		return false;

	if (g7221->bitrate != g7221_bitrate(rfmtp))
		return false;

	return true;
}
