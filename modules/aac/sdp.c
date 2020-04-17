/**
 * @file aac/sdp.c MPEG-4 AAC SDP Functions
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#include <strings.h>
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


/* check decoding compatibility of remote format */
bool aac_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg)
{
	(void)lfmtp;
	(void)arg;

	uint32_t plid;

	struct pl pl, val;

	if (!rfmtp)
		return false;

	pl_set_str(&pl, rfmtp);

	debug("aac: compare: %s\n", rfmtp);

	if (fmt_param_get(&pl, "mode", &val)) {
		if (strncasecmp("AAC-hbr", val.p, val.l))
			return false;
	}

	if (param_value(rfmtp, "streamType") != AAC_STREAMTYPE_AUDIO)
		return false;

	if (param_value(rfmtp, "sizeLength") != AAC_SIZELENGTH)
		return false;

	if (param_value(rfmtp, "indexLength") != AAC_INDEXLENGTH)
		return false;

	if (param_value(rfmtp, "indexDeltaLength") != AAC_INDEXDELTALENGTH)
		return false;

	if (param_value(rfmtp, "bitrate") < 8000 ||
	    param_value(rfmtp, "bitrate") > 576000)
		return false;

	switch (param_value(rfmtp, "constantDuration")) {
	case 120:
	case 128:
	case 240:
	case 256:
	case 480:
	case 512:
	case 960:
	case 1024:
	case 1920:
	case 2048:
		break;
	default:
		return false;
	}

	plid = param_value(rfmtp, "profile-level-id");
	if (!((plid >= 14 && plid <= 29) ||
	      (plid >= 41 && plid <= 52) ||
	      (plid >= 76 && plid <= 77)))
		return false;

	return true;
}
