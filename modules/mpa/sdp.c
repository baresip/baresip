/**
 * @file mpa/sdp.c mpa SDP Functions
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <baresip.h>
#include <string.h>
#include "mpa.h"


static void assign_if (uint32_t *v, const struct pl *pl,
		      uint32_t min, uint32_t max)
{
	const uint32_t val = pl_u32(pl);

	if (val < min || val > max)
		return;

	*v = val;
}


void mpa_decode_fmtp(struct mpa_param *prm, const char *fmtp)
{
	struct pl pl, val;

	if (!prm || !fmtp)
		return;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "bitrate", &val))
		assign_if (&prm->bitrate, &val, 8000, 384000);

	if (fmt_param_get(&pl, "samplerate", &val))
		assign_if (&prm->samplerate, &val, 16000, 48000);

	if (fmt_param_get(&pl, "layer", &val))
		assign_if (&prm->layer, &val, 1, 3);

	if (fmt_param_get(&pl, "mode", &val)) {

		if (!strncmp("stereo",val.p,val.l))
			prm->mode = STEREO;
		else if (!strncmp("joint_stereo",val.p,val.l))
			prm->mode = JOINT_STEREO;
		else if (!strncmp("single_channel",val.p,val.l))
			prm->mode = SINGLE_CHANNEL;
		else if (!strncmp("dual_channel",val.p,val.l))
			prm->mode = DUAL_CHANNEL;
	}
}

