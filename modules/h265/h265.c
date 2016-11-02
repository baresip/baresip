/**
 * @file h265.c H.265 Video Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#include <x265.h>
#include "h265.h"


/**
 * @defgroup h265 h265
 *
 * The H.265 video codec (aka HEVC)
 *
 * This is an experimental module adding support for H.265 video codec.
 * The encoder is using x265 and the decoder is using libavcodec.
 *
 *
 * References:
 *
 *    https://tools.ietf.org/html/rfc7798
 *    http://x265.org/
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


static int module_init(void)
{
	info("h265: using x265 %s %s\n",
	     x265_version_str, x265_build_info_str);

	avcodec_register_all();

	vidcodec_register(&h265);

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
