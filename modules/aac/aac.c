/**
 * @file aac.c AAC Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "aac.h"


static struct aucodec aac = {
	.name      = "MP4A-LATM",
	.srate     = AAC_SRATE,
	.crate     = 90000,
	.ch        = AAC_CHANNELS,
	.pch       = 1,
	.fmtp      = "profile-level-id=24;object=23;bitrate=64000",
	.encupdh   = aac_encode_update,
	.ench      = aac_encode_frm,
	.decupdh   = aac_decode_update,
	.dech      = aac_decode_frm,
};


static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), &aac);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&aac);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aac) = {
	"aac",
	"audio codec",
	module_init,
	module_close,
};
