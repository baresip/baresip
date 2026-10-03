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


/* The value of mode-set may contain spaces ("0, 1, 2") */
static bool mode_set_get(const char *fmtp, struct pl *ms)
{
	struct pl v;

	if (!fmtp || re_regex(fmtp, str_len(fmtp),
			      "mode-set[ \t]*=[ \t]*[0-9, \t]+",
			      NULL, NULL, &v))
		return false;

	while (v.l && (v.p[v.l-1] == ' ' || v.p[v.l-1] == '\t' ||
		       v.p[v.l-1] == ','))
		--v.l;

	*ms = v;
	return v.l > 0;
}


/* The highest mode in a mode-set must not be above the maxmode specified by
 * the server. In the case that the server didn't specify the mode, then the
 * max mode is used */
int amr_mode_set_max(const char *fmtp, int maxmode)
{
	struct pl ms;
	int best = -1;

	if (!mode_set_get(fmtp, &ms))
		return maxmode;

	while (ms.l) {
		struct pl num;

		if (re_regex(ms.p, ms.l, "[0-9]+", &num))
			break;

		if ((int)pl_u32(&num) <= maxmode && (int)pl_u32(&num) > best)
			best = pl_u32(&num);

		ms.l -= num.p + num.l - ms.p;
		ms.p  = num.p + num.l;
	}

	return best >= 0 ? best : maxmode;
}


int amr_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *arg)
{
	const struct amr_aucodec *amr_ac = arg;
	struct pl ms = PL_INIT;
	bool has_ms = false;

	if (!mb || !fmt || !amr_ac)
		return 0;

	/* An answer repeats the offer's mode-set (RFC 4867 8.3.1) */
	if (!offer)
		has_ms = mode_set_get(fmt->rparams, &ms);

	if (!amr_ac->aligned && !has_ms)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s %s%s%r\r\n", fmt->id,
			   amr_ac->aligned ? "octet-align=1" : "",
			   amr_ac->aligned && has_ms ? ";mode-set=" :
			   has_ms ? "mode-set=" : "",
			   &ms);
}
