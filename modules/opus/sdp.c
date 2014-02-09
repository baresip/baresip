/**
 * @file opus/sdp.c Opus SDP Functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <opus/opus.h>
#include "opus.h"


static void assign_if(opus_int32 *v, const struct pl *pl,
		      uint32_t min, uint32_t max)
{
	const uint32_t val = pl_u32(pl);

	if (val < min || val > max)
		return;

	*v = val;
}


void opus_decode_fmtp(struct opus_param *prm, const char *fmtp)
{
	struct pl pl, val;

	if (!prm || !fmtp)
		return;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "maxplaybackrate", &val))
		assign_if(&prm->srate, &val, 8000, 48000);

	if (fmt_param_get(&pl, "maxaveragebitrate", &val))
		assign_if(&prm->bitrate, &val, 6000, 510000);

	if (fmt_param_get(&pl, "stereo", &val))
		assign_if(&prm->stereo, &val, 0, 1);

	if (fmt_param_get(&pl, "cbr", &val))
		assign_if(&prm->cbr, &val, 0, 1);

	if (fmt_param_get(&pl, "useinbandfec", &val))
		assign_if(&prm->inband_fec, &val, 0, 1);

	if (fmt_param_get(&pl, "usedtx", &val))
		assign_if(&prm->dtx, &val, 0, 1);
}
