/**
 * @file twcc.c Transport-wide Congestion Control (TWCC)
 *
 * Copyright (C) 2025 Sebastian Reimers
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


static const char uri[] = "http://www.ietf.org/id/"
			  "draft-holmer-rmcat-transport-wide-cc-extensions-01";


static bool extmap_handler(const char *name, const char *value, void *arg)
{
	struct sdp_media *sdp = arg;
	struct sdp_extmap extmap;
	int err;
	(void)name;

	err = sdp_extmap_decode(&extmap, value);
	if (err) {
		warning("twcc: sdp_extmap_decode error (%m)\n", err);
		return false;
	}

	if (0 == pl_strcasecmp(&extmap.name, uri)) {
		err = sdp_media_set_lattr(sdp, true, "extmap", "%u %s",
					  extmap.id, uri);
		return true;
	}

	return false;
}


void twcc_handle_extmap(struct sdp_media *sdp)
{
	sdp_media_rattr_apply(sdp, "extmap", extmap_handler, sdp);
}
