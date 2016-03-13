/**
 * @file amr/sdp.c AMR SDP Functions
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "amr.h"


static bool amr_octet_align(const char *fmtp)
{
	struct pl pl, oa;

	if (!fmtp)
		return false;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "octet-align", &oa))
		return 0 == pl_strcmp(&oa, "1");

	return false;
}


int amr_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *arg)
{
	const struct aucodec *ac = arg;
	(void)offer;

	if (!mb || !fmt || !ac)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s octet-align=1\r\n",
			   fmt->id);
}


bool amr_fmtp_cmp(const char *lfmtp, const char *rfmtp, void *arg)
{
	const struct aucodec *ac = arg;
	(void)lfmtp;

	if (!ac)
		return false;

	if (!amr_octet_align(rfmtp)) {
		info("amr: octet-align mode is required\n");
		return false;
	}

	return true;
}
