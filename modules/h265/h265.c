/**
 * @file h265.c H.265 Video Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include "h265.h"


/**
 * @defgroup h265 h265
 *
 * The H.265 video codec (aka HEVC)
 *
 * This module adds support for H.265 video codec.
 * The encoder and decoder is using libavcodec.
 *
 *
 * References:
 *
 *    https://tools.ietf.org/html/rfc7798
 *    https://www.ffmpeg.org/
 */


static struct vidcodec h265 = {
	.name      = "H265",
	.fmtp      = "profile-id=1",
	.encupdh   = h265_encode_update,
	.ench      = h265_encode,
	.decupdh   = h265_decode_update,
	.dech      = h265_decode,
};


AVCodec *h265_encoder;
AVCodec *h265_decoder;


static int module_init(void)
{
	char enc[64] = "libx265";
	char dec[64] = "hevc";

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif

	conf_get_str(conf_cur(), "h265_encoder", enc, sizeof(enc));
	conf_get_str(conf_cur(), "h265_decoder", dec, sizeof(dec));

	h265_encoder = avcodec_find_encoder_by_name(enc);
	if (!h265_encoder) {
		warning("h265: encoder not found (%s)\n", enc);
		return ENOENT;
	}

	h265_decoder = avcodec_find_decoder_by_name(dec);
	if (!h265_decoder) {
		warning("h265: decoder not found (%s)\n", dec);
		return ENOENT;
	}

	info("h265: using encoder '%s' -- %s\n",
	     h265_encoder->name, h265_encoder->long_name);
	info("h265: using decoder '%s' -- %s\n",
	     h265_decoder->name, h265_decoder->long_name);

	vidcodec_register(baresip_vidcodecl(), &h265);

	return 0;
}


static int module_close(void)
{
	vidcodec_unregister(&h265);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(h265) = {
	"h265",
	"vidcodec",
	module_init,
	module_close,
};
