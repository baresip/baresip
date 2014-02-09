/**
 * @file vpx.c  VP8 video codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#define VPX_CODEC_DISABLE_COMPAT 1
#define VPX_DISABLE_CTRL_TYPECHECKS 1
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>
#include "vp8.h"


/*
 * Experimental support for WebM VP8 video codec:
 *
 *     http://www.webmproject.org/
 *
 *     http://tools.ietf.org/html/draft-ietf-payload-vp8-08
 */


static struct vp8_vidcodec vpx = {
	.vc = {
		.name      = "VP8",
		.encupdh   = vp8_encode_update,
		.ench      = vp8_encode,
		.decupdh   = vp8_decode_update,
		.dech      = vp8_decode,
		.fmtp_ench = vp8_fmtp_enc,
	},
	.max_fs = 3600
};


static int module_init(void)
{
	vidcodec_register((struct vidcodec *)&vpx);
	return 0;
}


static int module_close(void)
{
	vidcodec_unregister((struct vidcodec *)&vpx);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(vpx) = {
	"vpx",
	"codec",
	module_init,
	module_close
};
