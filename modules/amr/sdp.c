/**
 * @file amr/sdp.c AMR SDP Functions
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "amr.h"


bool amr_octet_align(const char *fmtp)
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
	const struct amr_aucodec *amr_ac = arg;
	(void)offer;

	if (!mb || !fmt || !amr_ac)
		return 0;

	if (amr_ac->aligned) {
		return mbuf_printf(mb, "a=fmtp:%s octet-align=1\r\n",
			   fmt->id);
	}

	return 0;
}
