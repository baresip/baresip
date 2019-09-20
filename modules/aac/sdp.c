/**
 * @file aac/sdp.c AAC SDP Functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <fdk-aac/FDK_audio.h>
#include "aac.h"


static unsigned param_value(const char *fmtp, const char *name)
{
	struct pl pl, val;

	if (!fmtp || !name)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, name, &val))
		return pl_u32(&val);

	return 0;
}


int aac_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *arg)
{
	(void)offer;
	(void)arg;

	if (!mb || !fmt)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s "
			   "profile-level-id=24;object=%i;bitrate=%u\r\n",
			   fmt->id, AOT_ER_AAC_LD, AAC_BITRATE);
}


bool aac_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg)
{
	(void)lfmtp;
	(void)arg;

	if (param_value(rfmtp, "object") != AOT_ER_AAC_LD)
		return false;

	if (param_value(rfmtp, "bitrate") != AAC_BITRATE)
		return false;

	return true;
}
