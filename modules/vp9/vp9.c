/**
 * @file vp9.c  VP9 video codec
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vp9.h"


/**
 * @defgroup vp9 vp9
 *
 * The VP9 video codec
 *
 * This module implements the VP9 video codec that is compatible
 * with the WebRTC standard.
 *
 * Libvpx version 1.3.0 or later is required.
 *
 *
 * References:
 *
 *     http://www.webmproject.org/
 *
 *     draft-ietf-payload-vp9-07
 */


static struct vp9_vidcodec vp9 = {
	.vc = {
		.name      = "VP9",
		.encupdh   = vp9_encode_update,
		.ench      = vp9_encode,
		.decupdh   = vp9_decode_update,
		.dech      = vp9_decode,
		.fmtp_ench = vp9_fmtp_enc,
	},
	.max_fs = 3600
};


static int module_init(void)
{
	vidcodec_register(baresip_vidcodecl(), (struct vidcodec *)&vp9);
	return 0;
}


static int module_close(void)
{
	vidcodec_unregister((struct vidcodec *)&vp9);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vp9) = {
	"vp9",
	"codec",
	module_init,
	module_close
};
