/**
 * @file g7221.c G.722.1 Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "g7221.h"


static struct g7221_aucodec g7221 = {
	.ac = {
		.name      = "G7221",
		.srate     = 16000,
		.crate     = 16000,
		.ch        = 1,
		.pch       = 1,
		.encupdh   = g7221_encode_update,
		.ench      = g7221_encode,
		.decupdh   = g7221_decode_update,
		.dech      = g7221_decode,
		.fmtp_ench = g7221_fmtp_enc,
		.fmtp_cmph = g7221_fmtp_cmp,
	},
	.bitrate = 32000,
};


static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), (struct aucodec *)&g7221);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister((struct aucodec *)&g7221);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(g7221) = {
	"g7221",
	"audio codec",
	module_init,
	module_close,
};
