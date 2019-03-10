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


AVCodec *h265_encoder;


static int module_init(void)
{
	char enc[64] = "libx265";

	av_log_set_level(AV_LOG_WARNING);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif

	conf_get_str(conf_cur(), "h265_encoder", enc, sizeof(enc));

	h265_encoder = avcodec_find_encoder_by_name(enc);
	if (!h265_encoder) {
		warning("h265: encoder not found (%s)\n", enc);
		return ENOENT;
	}

	info("h265: using encoder '%s' (%s)\n", h265_encoder->name,
	     h265_encoder->long_name);

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
