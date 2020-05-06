/**
 * @file avcodec.c  Video codecs using libavcodec
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <libavutil/pixdesc.h>
#include <libavcodec/avcodec.h>
#include "h26x.h"
#include "avcodec.h"


/**
 * @defgroup avcodec avcodec
 *
 * Video codecs using libavcodec
 *
 * This module implements H.263, H.264 and MPEG4 video codecs
 * using libavcodec from FFmpeg or libav projects.
 *
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


AVCodec *avcodec_h264enc;             /* optional; specified H.264 encoder */
AVCodec *avcodec_h264dec;             /* optional; specified H.264 decoder */
AVCodec *avcodec_h265enc;
AVCodec *avcodec_h265dec;


#if LIBAVUTIL_VERSION_MAJOR >= 56
AVBufferRef *avcodec_hw_device_ctx = NULL;
enum AVPixelFormat avcodec_hw_pix_fmt;
enum AVHWDeviceType avcodec_hw_type = AV_HWDEVICE_TYPE_NONE;
#endif


int avcodec_resolve_codecid(const char *s)
{
	if (0 == str_casecmp(s, "H263"))
		return AV_CODEC_ID_H263;
	else if (0 == str_casecmp(s, "H264"))
		return AV_CODEC_ID_H264;
	else if (0 == str_casecmp(s, "MP4V-ES"))
		return AV_CODEC_ID_MPEG4;
#ifdef AV_CODEC_ID_H265
	else if (0 == str_casecmp(s, "H265"))
		return AV_CODEC_ID_H265;
#endif
	else
		return AV_CODEC_ID_NONE;
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
	.name      = "H264",
	.variant   = "packetization-mode=0",
	.encupdh   = avcodec_encode_update,
	.ench      = avcodec_encode,
	.decupdh   = avcodec_decode_update,
	.dech      = avcodec_decode_h264,
	.fmtp_ench = avcodec_h264_fmtp_enc,
	.fmtp_cmph = avcodec_h264_fmtp_cmp,
};

static struct vidcodec h264_1 = {
	.name      = "H264",
	.variant   = "packetization-mode=1",
	.encupdh   = avcodec_encode_update,
	.ench      = avcodec_encode,
	.decupdh   = avcodec_decode_update,
	.dech      = avcodec_decode_h264,
	.fmtp_ench = avcodec_h264_fmtp_enc,
	.fmtp_cmph = avcodec_h264_fmtp_cmp,
};

static struct vidcodec h263 = {
	.pt        = "34",
	.name      = "H263",
	.encupdh   = avcodec_encode_update,
	.ench      = avcodec_encode,
	.decupdh   = avcodec_decode_update,
	.dech      = avcodec_decode_h263,
	.fmtp_ench = h263_fmtp_enc,
};

static struct vidcodec mpg4 = {
	.name      = "MP4V-ES",
	.encupdh   = avcodec_encode_update,
	.ench      = avcodec_encode,
	.decupdh   = avcodec_decode_update,
	.dech      = avcodec_decode_mpeg4,
	.fmtp_ench = mpg4_fmtp_enc,
};

static struct vidcodec h265 = {
	.name      = "H265",
	.fmtp      = "profile-id=1",
	.encupdh   = avcodec_encode_update,
	.ench      = avcodec_encode,
	.decupdh   = avcodec_decode_update,
	.dech      = avcodec_decode_h265,
};


static int module_init(void)
{
	struct list *vidcodecl = baresip_vidcodecl();
	char h264enc[64] = "libx264";
	char h264dec[64] = "h264";
	char h265enc[64] = "libx265";
	char h265dec[64] = "hevc";
#if LIBAVUTIL_VERSION_MAJOR >= 56
	char hwaccel[64];
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 10, 0)
	avcodec_init();
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
	avcodec_register_all();
#endif

	conf_get_str(conf_cur(), "avcodec_h264enc", h264enc, sizeof(h264enc));
	conf_get_str(conf_cur(), "avcodec_h264dec", h264dec, sizeof(h264dec));
	conf_get_str(conf_cur(), "avcodec_h265enc", h265enc, sizeof(h265enc));
	conf_get_str(conf_cur(), "avcodec_h265dec", h265dec, sizeof(h265dec));

	avcodec_h264enc = avcodec_find_encoder_by_name(h264enc);
	if (!avcodec_h264enc) {
		warning("avcodec: h264 encoder not found (%s)\n", h264enc);
	}

	avcodec_h264dec = avcodec_find_decoder_by_name(h264dec);
	if (!avcodec_h264dec) {
		warning("avcodec: h264 decoder not found (%s)\n", h264dec);
	}

	avcodec_h265enc = avcodec_find_encoder_by_name(h265enc);
	avcodec_h265dec = avcodec_find_decoder_by_name(h265dec);

	if (avcodec_h264enc || avcodec_h264dec) {
		vidcodec_register(vidcodecl, &h264);
		vidcodec_register(vidcodecl, &h264_1);
	}

	if (avcodec_find_decoder(AV_CODEC_ID_H263))
		vidcodec_register(vidcodecl, &h263);

	if (avcodec_find_decoder(AV_CODEC_ID_MPEG4))
		vidcodec_register(vidcodecl, &mpg4);

	if (avcodec_h265enc || avcodec_h265dec)
		vidcodec_register(vidcodecl, &h265);

	if (avcodec_h264enc) {
		info("avcodec: using H.264 encoder '%s' -- %s\n",
		     avcodec_h264enc->name, avcodec_h264enc->long_name);
	}
	if (avcodec_h264dec) {
		info("avcodec: using H.264 decoder '%s' -- %s\n",
		     avcodec_h264dec->name, avcodec_h264dec->long_name);
	}

	if (avcodec_h265enc) {
		info("avcodec: using H.265 encoder '%s' -- %s\n",
		     avcodec_h265enc->name, avcodec_h265enc->long_name);
	}
	if (avcodec_h265dec) {
		info("avcodec: using H.265 decoder '%s' -- %s\n",
		     avcodec_h265dec->name, avcodec_h265dec->long_name);
	}

#if LIBAVUTIL_VERSION_MAJOR >= 56
	/* common for encode/decode */
	if (0 == conf_get_str(conf_cur(), "avcodec_hwaccel",
			      hwaccel, sizeof(hwaccel))) {

		enum AVHWDeviceType type;
		int ret;
		int i;

		info("avcodec: enable hwaccel using '%s'\n", hwaccel);

		type = av_hwdevice_find_type_by_name(hwaccel);
		if (type == AV_HWDEVICE_TYPE_NONE) {

			warning("avcodec: Device type"
				" '%s' is not supported.\n", hwaccel);

			return ENOSYS;
		}

		for (i = 0;; i++) {
			const AVCodecHWConfig *config;

			config = avcodec_get_hw_config(avcodec_h264dec, i);
			if (!config) {
				warning("avcodec: Decoder does not"
					" support device type %s.\n",
					av_hwdevice_get_type_name(type));
				return ENOSYS;
			}

			if (config->methods
			    & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX
			    &&
			    config->device_type == type) {

				avcodec_hw_pix_fmt = config->pix_fmt;

				info("avcodec: decode: using hardware"
				     " pixel format '%s'\n",
				     av_get_pix_fmt_name(config->pix_fmt));
				break;
			}
		}

		ret = av_hwdevice_ctx_create(&avcodec_hw_device_ctx, type,
					     NULL, NULL, 0);
		if (ret < 0) {
			warning("avcodec: Failed to create HW device (%s)\n",
				av_err2str(ret));
			return ENOTSUP;
		}

		avcodec_hw_type = type;
	}
#endif

	return 0;
}


static int module_close(void)
{
	vidcodec_unregister(&h265);
	vidcodec_unregister(&mpg4);
	vidcodec_unregister(&h263);
	vidcodec_unregister(&h264);
	vidcodec_unregister(&h264_1);

#if LIBAVUTIL_VERSION_MAJOR >= 56
	if (avcodec_hw_device_ctx)
		av_buffer_unref(&avcodec_hw_device_ctx);
#endif

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avcodec) = {
	"avcodec",
	"codec",
	module_init,
	module_close
};
