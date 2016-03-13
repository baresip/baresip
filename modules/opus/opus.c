/**
 * @file opus.c Opus Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <opus/opus.h>
#include "opus.h"


/**
 * @defgroup opus opus
 *
 * The OPUS audio codec
 *
 * Supported version: libopus 1.0.0 or later
 *
 * References:
 *
 *    RFC 6716  Definition of the Opus Audio Codec
 *    RFC 7587  RTP Payload Format for the Opus Speech and Audio Codec
 *
 *    http://opus-codec.org/downloads/
 */


static struct aucodec opus = {
	.name      = "opus",
	.srate     = 48000,
	.ch        = 2,
	.fmtp      = "stereo=1;sprop-stereo=1",
	.encupdh   = opus_encode_update,
	.ench      = opus_encode_frm,
	.decupdh   = opus_decode_update,
	.dech      = opus_decode_frm,
	.plch      = opus_decode_pkloss,
};


static int module_init(void)
{
	struct conf *conf = conf_cur();
	uint32_t value;
	static char fmtp[128];

	if (0 == conf_get_u32(conf, "opus_bitrate", &value)) {
		(void)re_snprintf(fmtp, sizeof(fmtp),
				"stereo=1;sprop-stereo=1;maxaveragebitrate=%d",
				value);
		opus.fmtp = fmtp;
	}

	aucodec_register(&opus);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&opus);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(opus) = {
	"opus",
	"audio codec",
	module_init,
	module_close,
};
