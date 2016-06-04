/**
 * @file vp9/sdp.c VP9 SDP Functions
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "vp9.h"


uint32_t vp9_max_fs(const char *fmtp)
{
	struct pl pl, max_fs;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "max-fs", &max_fs))
		return pl_u32(&max_fs);

	return 0;
}


int vp9_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *arg)
{
	const struct vp9_vidcodec *vp9 = arg;
	(void)offer;

	if (!mb || !fmt || !vp9 || !vp9->max_fs)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s max-fs=%u\r\n",
			   fmt->id, vp9->max_fs);
}
