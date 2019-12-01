/**
 * @file aptx/sdp.c aptX SDP Functions
 *
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#include <re.h>
#include <baresip.h>
#include <openaptx.h>
#include "aptx.h"


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


int aptx_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt, bool offer,
                  void *arg)
{
	(void)offer;
	(void)arg;

	if (!mb || !fmt)
		return 0;

	return mbuf_printf(mb,
	                   "a=fmtp:%s "
	                   "variant=%s; bitresolution=%u;\r\n",
	                   fmt->id,
	                   APTX_VARIANT == APTX_VARIANT_HD ? "hd" : "standard",
	                   APTX_VARIANT == APTX_VARIANT_HD ? 24 : 16);
}


bool aptx_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg)
{
	(void)lfmtp;
	(void)arg;

	/*
	if (param_value(rfmtp, "variant") != APTX_VARIANT)
	        return false;
	*/

	if (param_value(rfmtp, "bitresolution") !=
	    (APTX_VARIANT == APTX_VARIANT_HD ? 24 : 16))
		return false;

	return true;
}
