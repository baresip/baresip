/**
 * @file avcodec.c  Video codecs using libavcodec
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
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
 * This module implements H.264 and H.265 video codecs
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


const AVCodec *avcodec_h264enc;      /* optional; specified H.264 encoder */
const AVCodec *avcodec_h264dec;      /* optional; specified H.264 decoder */
const AVCodec *avcodec_h265enc;
const AVCodec *avcodec_h265dec;


AVBufferRef *avcodec_hw_device_ctx = NULL;
enum AVPixelFormat avcodec_hw_pix_fmt;
enum AVHWDeviceType avcodec_hw_type = AV_HWDEVICE_TYPE_NONE;


int avcodec_resolve_codecid(const char *s)
{
	if (0 == str_casecmp(s, "H264"))
		return AV_CODEC_ID_H264;
	else if (0 == str_casecmp(s, "H265"))
		return AV_CODEC_ID_H265;
	else
		return AV_CODEC_ID_NONE;
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
	.packetizeh= avcodec_packetize,
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
	.packetizeh= avcodec_packetize,
};

static struct vidcodec h265 = {
	.name      = "H265",
	.fmtp      = "profile-id=1",
	.encupdh   = avcodec_encode_update,
	.ench      = avcodec_encode,
	.decupdh   = avcodec_decode_update,
	.dech      = avcodec_decode_h265,
	.packetizeh= avcodec_packetize,
};


static int module_init(void)
{
	struct list *vidcodecl = baresip_vidcodecl();
	char h264enc[64] = "libx264";
	char h264dec[64] = "h264";
	char h265enc[64] = "libx265";
	char h265dec[64] = "hevc";
	char hwaccel[64];

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

	/* common for encode/decode */
	if (0 == conf_get_str(conf_cur(), "avcodec_hwaccel",
			      hwaccel, sizeof(hwaccel))) {

		enum AVHWDeviceType type;
		int ret;

		info("avcodec: enable hwaccel using '%s'\n", hwaccel);

		type = av_hwdevice_find_type_by_name(hwaccel);
		if (type == AV_HWDEVICE_TYPE_NONE) {

			warning("avcodec: Device type"
				" '%s' is not supported.\n", hwaccel);

			info("Available device types:\n");
			while ((type = av_hwdevice_iterate_types(type))
				!= AV_HWDEVICE_TYPE_NONE)
				info("    %s\n",
				     av_hwdevice_get_type_name(type));
			info("\n");

			return ENOSYS;
		}

		for (int i = 0;; i++) {
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

	return 0;
}


static int module_close(void)
{
	vidcodec_unregister(&h265);
	vidcodec_unregister(&h264);
	vidcodec_unregister(&h264_1);

	if (avcodec_hw_device_ctx)
		av_buffer_unref(&avcodec_hw_device_ctx);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(avcodec) = {
	"avcodec",
	"codec",
	module_init,
	module_close
};
