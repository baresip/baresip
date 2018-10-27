/**
 * @file avcodec.c  Video codecs using libavcodec
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavcodec/avcodec.h>
#ifdef USE_X264
#include <x264.h>
#endif
#include "h26x.h"
#include "avcodec.h"


/**
 * @defgroup avcodec avcodec
 *
 * Video codecs using libavcodec
 *
 * This module implements H.263, H.264 and MPEG4 video codecs
 * using libavcodec from FFmpeg or libav projects, and libx264.
 *
 * Build options:
 *
 \verbatim
      $ make USE_X264=1    ; enable direct usage of libx264
      $ make USE_X264=     ; use H.264 encoder from libavcodec
 \endverbatim
 *
 * Config options:
 *
 \verbatim
      avcodec_h264enc  <NAME>  ; e.g. h264_nvenc, h264_videotoolbox
      avcodec_h264dec  <NAME>  ; e.g. h264_cuvid, h264_vda, h264_qsv
 \endverbatim
 *
 * References:
 *
 *     http://ffmpeg.org
 *
 *     https://libav.org
 *
 *     RTP Payload Format for H.264 Video
 *     https://tools.ietf.org/html/rfc6184
 */


const uint8_t h264_level_idc = 0x1f;
AVCodec *avcodec_h264enc;             /* optional; specified H.264 encoder */
AVCodec *avcodec_h264dec;             /* optional; specified H.264 decoder */


int avcodec_resolve_codecid(const char *s)
{
	if (0 == str_casecmp(s, "H263"))
		return AV_CODEC_ID_H263;
	else if (0 == str_casecmp(s, "H264"))
		return AV_CODEC_ID_H264;
	else if (0 == str_casecmp(s, "MP4V-ES"))
		return AV_CODEC_ID_MPEG4;
	else
		return AV_CODEC_ID_NONE;
}


static uint32_t packetization_mode(const char *fmtp)
{
	struct pl pl, mode;

	if (!fmtp)
		return 0;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "packetization-mode", &mode))
		return pl_u32(&mode);

	return 0;
}


static int h264_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			 bool offer, void *arg)
{
	struct vidcodec *vc = arg;
	const uint8_t profile_idc = 0x42; /* baseline profile */
	const uint8_t profile_iop = 0x80;
	(void)offer;

	if (!mb || !fmt || !vc)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s"
			   " packetization-mode=0"
			   ";profile-level-id=%02x%02x%02x"
			   "\r\n",
			   fmt->id, profile_idc, profile_iop, h264_level_idc);
}


static bool h264_fmtp_cmp(const char *fmtp1, const char *fmtp2, void *data)
{
	(void)data;

	return packetization_mode(fmtp1) == packetization_mode(fmtp2);
}


static int h263_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			 bool offer, void *arg)
{
	(void)offer;
	(void)arg;

	if (!mb || !fmt)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s CIF=1;CIF4=1\r\n", fmt->id);
}


static int mpg4_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
			 bool offer, void *arg)
{
	(void)offer;
	(void)arg;

	if (!mb || !fmt)
		return 0;

	return mbuf_printf(mb, "a=fmtp:%s profile-level-id=3\r\n", fmt->id);
}


static struct vidcodec h264 = {
	LE_INIT,
	NULL,
	"H264",
	"packetization-mode=0",
	NULL,
	encode_update,
#ifdef USE_X264
	encode_x264,
#else
	encode,
#endif
	decode_update,
	decode_h264,
	h264_fmtp_enc,
	h264_fmtp_cmp,
};

static struct vidcodec h263 = {
	LE_INIT,
	"34",
	"H263",
	NULL,
	NULL,
	encode_update,
	encode,
	decode_update,
	decode_h263,
	h263_fmtp_enc,
	NULL,
};

static struct vidcodec mpg4 = {
	LE_INIT,
	NULL,
	"MP4V-ES",
	NULL,
	NULL,
	encode_update,
	encode,
	decode_update,
	decode_mpeg4,
	mpg4_fmtp_enc,
	NULL,
};


static int module_init(void)
{
	struct list *vidcodecl = baresip_vidcodecl();
	char h264enc[64];
	char h264dec[64];

#ifdef USE_X264
	debug("avcodec: x264 build %d\n", X264_BUILD);
#else
	debug("avcodec: using libavcodec H.264 encoder\n");
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 10, 0)
	avcodec_init();
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif

	if (0 == conf_get_str(conf_cur(), "avcodec_h264dec",
			      h264dec, sizeof(h264dec))) {

		info("avcodec: using h264 decoder by name (%s)\n", h264dec);

		avcodec_h264dec = avcodec_find_decoder_by_name(h264dec);
		if (!avcodec_h264dec) {
			warning("avcodec: h264 decoder not found (%s)\n",
				h264dec);
			return ENOENT;
		}
		vidcodec_register(vidcodecl, &h264);
	}
	else {
		if (avcodec_find_decoder(AV_CODEC_ID_H264))
			vidcodec_register(vidcodecl, &h264);
	}

	if (avcodec_find_decoder(AV_CODEC_ID_H263))
		vidcodec_register(vidcodecl, &h263);

	if (avcodec_find_decoder(AV_CODEC_ID_MPEG4))
		vidcodec_register(vidcodecl, &mpg4);

	if (0 == conf_get_str(conf_cur(), "avcodec_h264enc",
			      h264enc, sizeof(h264enc))) {

		info("avcodec: using h264 encoder by name (%s)\n", h264enc);

		avcodec_h264enc = avcodec_find_encoder_by_name(h264enc);
		if (!avcodec_h264enc) {
			warning("avcodec: h264 encoder not found (%s)\n",
				h264enc);
			return ENOENT;
		}
	}

	return 0;
}


static int module_close(void)
{
	vidcodec_unregister(&mpg4);
	vidcodec_unregister(&h263);
	vidcodec_unregister(&h264);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avcodec) = {
	"avcodec",
	"codec",
	module_init,
	module_close
};
