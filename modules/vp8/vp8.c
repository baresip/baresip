/**
 * @file vp8.c VP8 Video Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "vp8.h"


/**
 * @defgroup vp8 vp8
 *
 * The VP8 video codec
 *
 * This module implements the VP8 video codec that is compatible
 * with the WebRTC standard.
 *
 * References:
 *
 *     http://www.webmproject.org/
 *
 *     https://tools.ietf.org/html/rfc7741
 */


static struct vp8_vidcodec vp8 = {
	.vc = {
		.name      = "VP8",
		.encupdh   = vp8_encode_update,
		.ench      = vp8_encode,
		.decupdh   = vp8_decode_update,
		.dech      = vp8_decode,
		.fmtp_ench = vp8_fmtp_enc,
	},
	.max_fs   = 3600,
};


static int module_init(void)
{
	vidcodec_register(baresip_vidcodecl(), (struct vidcodec *)&vp8);

	return 0;
}


static int module_close(void)
{
	vidcodec_unregister((struct vidcodec *)&vp8);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vp8) = {
	"vp8",
	"codec",
	module_init,
	module_close
};
