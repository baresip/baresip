/**
 * @file aac.c MPEG-4 AAC Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#include <re.h>
#include <baresip.h>
#include <fdk-aac/FDK_audio.h>
#include <fdk-aac/aacenc_lib.h>
#include "aac.h"


/**
 * @defgroup aac aac
 *
 * Advanced Audio Coding (AAC) audio codec
 *
 * Supported version: libfdk-aac 0.1.6 or later
 *
 * Configuration options:
 *
 \verbatim
  aac_samplerate     48000	# Encoded/decoded audio sample rate [Hz]
  aac_channels           1	# Encoded/decoded audio channels
  aac_aot               23	# Audio Object Type (AOT)
                        	#  2: MPEG-4 AAC Low Complexity (AAC-LC)
                        	#  5: MPEG-4 AAC Low Complexity with
                        	#     Spectral Band Replication (HE-AAC)
                        	# 29: MPEG-4 AAC Low Complexity with
                        	#     Spectral Band Replication and
                        	#     Parametric Stereo (HE-AAC v2)
                        	# 23: MPEG-4 AAC Low-Delay (AAC-LD)
                        	# 39: MPEG-4 AAC Enhanced Low-Delay (AAC-ELD)
  aac_bitrate       128000	# Average bitrate in [bps]
  aac_constantduration 480	# Coded PCM frame size
                        	# 1024 or 960 for AAC-LC
                        	# 2048 or 1920 for HE-AAC (v2)
                        	# 512 or 480 for AAC-LD and AAC-ELD
 \endverbatim
 *
 * References:
 *
 *    RFC 3640  RTP Payload Format for Transport of MPEG-4 Elementary Streams
 *
 *
 * ToDo:
 * 	- Support multiple access units per packet
 * 	- Add support for AAC-lbr
 * 	- Find and fix problem with fdk-aac HE-AAC v2 encoding
 * 	- Multichannel encoding (> stereo)
 * 	- SDP stereo and mono offer
 * 	- Find a way to set dynamic audio i/o ptime in samples for min. delay
 */

static char fmtp_local[256] = "";
static char fmtp_mirror[256];

uint32_t aac_samplerate, aac_channels, aac_aot;
uint32_t aac_bitrate, aac_profile, aac_constantduration;


static struct aucodec aac = {
	.name      = "mpeg4-generic",
	.encupdh   = aac_encode_update,
	.ench      = aac_encode_frm,
	.decupdh   = aac_decode_update,
	.dech      = aac_decode_frm,
	.fmtp_ench = aac_fmtp_enc,
	.fmtp_cmph = aac_fmtp_cmp,
/* try to make sure PCM audio buffer is always <= 120 samples */
	.ptime     = 2,      /* 96 samples per channel @ 48000 hz */
};


void aac_encode_fmtp(const struct aac_param *prm)
{
	(void)re_snprintf(fmtp_local, sizeof(fmtp_local),
	                  "streamType=%d"
	                  "; profile-level-id=%u"
	                  "; config=%s"
	                  "; mode=%s"
	                  "; constantDuration=%u"
	                  "; sizeLength=%u"
	                  "; indexLength=%u"
	                  "; indexDeltaLength=%u"
	                  "; bitrate=%u",
			  AAC_STREAMTYPE_AUDIO,
	                  prm->profile_level_id, prm->config, "AAC-hbr",
	                  prm->constantduration, AAC_SIZELENGTH,
	                  AAC_INDEXLENGTH, AAC_INDEXDELTALENGTH,
	                  prm->bitrate);
}


/* Parse remote fmtp string and map it to aac_param struct */
void aac_decode_fmtp(struct aac_param *prm, const char *fmtp)
{
	struct pl pl, val;

	if (!prm || !fmtp)
		return;

	pl_set_str(&pl, fmtp);

	if (fmt_param_get(&pl, "profile-level-id", &val))
		prm->profile_level_id = pl_u32(&val);

	if (fmt_param_get(&pl, "constantDuration", &val))
		prm->constantduration = pl_u32(&val);

	if (fmt_param_get(&pl, "bitrate", &val))
		prm->bitrate = pl_u32(&val);

	if (fmt_param_get(&pl, "config", &val))
		(void)pl_strcpy(&val, &prm->config[0], sizeof(prm->config));
}


/* describe local encoded format to remote */
int aac_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt, bool offer,
                 void *arg)
{
	bool mirror;

	(void)offer;
	(void)arg;

	if (!mb || !fmt)
		return 0;

	mirror = !offer && str_isset(fmtp_mirror);

	return mbuf_printf(mb, "a=fmtp:%s %s\r\n",
			   fmt->id, mirror ? fmtp_mirror : fmtp_local);
}


void aac_mirror_params(const char *x)
{
	debug("aac: mirror parameters: \"%s\"\n", x);

	str_ncpy(fmtp_mirror, x, sizeof(fmtp_mirror));
}


static int module_init(void)
{
	struct conf *conf = conf_cur();
	struct aac_param prm;
	HANDLE_AACENCODER enc;
	AACENC_InfoStruct enc_info;
	AACENC_ERROR error;
	uint32_t aac_ratio;

	/* default encoder configuration */
	aac_samplerate = 48000;
	aac_channels = 2;
	aac_aot = AOT_ER_AAC_LD;
	aac_bitrate = 128000;
	aac_constantduration = 480;

	/* encoder configuration from config file */
	(void)conf_get_u32(conf, "aac_samplerate",
	                   &aac_samplerate);
	(void)conf_get_u32(conf, "aac_channels",
	                   &aac_channels);
	(void)conf_get_u32(conf, "aac_aot",
	                   &aac_aot);
	(void)conf_get_u32(conf, "aac_bitrate",
	                   &aac_bitrate);
	(void)conf_get_u32(conf, "aac_constantduration",
	                   &aac_constantduration);

	if (aac_channels < 1 || aac_channels > 2) {
		aac_channels = 2;
	}

	aac.ch = aac_channels;
	aac.pch = aac_channels;

	switch (aac_samplerate) {
		case 8000:
		case 11025:
		case 12000:
		case 16000:
		case 22050:
		case 24000:
		case 32000:
		case 44100:
		case 48000:
		case 64000:
		case 88200:
		case 96000:
			break;
		default:
			aac_samplerate = 48000;
			break;
	}

	aac.srate = aac_samplerate;
	aac.crate = aac_samplerate;

	switch (aac_aot) {
	case AOT_AAC_LC:
		/*  2: MPEG-4 AAC Low Complexity */
		aac_profile = HIGH_QUALITY_AUDIO_PROFILE;
		aac_constantduration = 1024;
		aac_ratio = 1;
		break;
	case AOT_SBR:
		/*  5: MPEG-4 AAC Low Complexity with Spectral Band
		       Replication (HE-AAC) */
		aac_profile = HIGH_EFFICIENCY_AAC_PROFILE;
		aac_constantduration = 2048;
		aac_ratio = 2;
		break;
	case AOT_PS: /* Stereo only! */
		/* 29: MPEG-4 AAC Low Complexity with Spectral Band
		       Replication and Parametric Stereo (HE-AAC v2) */
		aac_profile = HIGH_EFFICIENCY_AAC_V2_PROFILE;
		aac_constantduration = 2048;
		aac_ratio = 2;
		aac_channels = 2;
		break;
	case AOT_ER_AAC_LD:
		/* 23: MPEG-4 AAC Low-Delay */
		aac_profile = LOW_DELAY_AUDIO_PROFILE;
		aac_ratio = 1;
		if (aac_constantduration != 480 &&
		    aac_constantduration != 512)
			aac_constantduration = 480;
		break;
	case AOT_ER_AAC_ELD:
		/* 39: MPEG-4 AAC Enhanced Low-Delay */
		aac_profile = ENHANCED_LOW_DELAY_AUDIO_PROFILE;
		if (aac_channels == 2) ++aac_profile;
		aac_ratio = 1;
		switch (aac_constantduration) {
		case 120:
		case 128:
		case 240:
		case 256:
		case 480:
		case 512:
			break;
		default:
			aac_constantduration = 120;
			break;
		}
		break;
	default:
		warning("AAC Audio object types 2 (AAC-LC), 5 "
			"(HE-AAC), 29 (HE-AAC v2), 23 (AAC-LD) "
			"and 39 (AAC-ELD) are allowed.\n");
		return EINVAL;
	}

	error = aacEncOpen(&enc, 0, 0);
	if (error != AACENC_OK) {
		warning("aac: Unable to open the encoder (0x%x)\n",
			error);
		return ENOMEM;
	}

	/* set mandatory encoder params: */
	error |= aacEncoder_SetParam(enc, AACENC_AOT, aac_aot);
	error |= aacEncoder_SetParam(enc, AACENC_SAMPLERATE, aac.srate);
	error |= aacEncoder_SetParam(enc, AACENC_CHANNELMODE, aac.ch);
	error |= aacEncoder_SetParam(enc, AACENC_BITRATE, aac_bitrate);
	error |= aacEncoder_SetParam(enc, AACENC_TRANSMUX, TT_MP4_RAW);
	/* set object specific encoder params: */
	error |= aacEncoder_SetParam(enc, AACENC_GRANULE_LENGTH,
	                             aac_constantduration/aac_ratio);
	error |= aacEncEncode(enc, NULL, NULL, NULL, NULL);
	error |= aacEncInfo(enc, &enc_info);
	if (error != AACENC_OK) {
		return EINVAL;
	}

	prm.constantduration = enc_info.frameLength;
	prm.bitrate = aacEncoder_GetParam(enc, AACENC_BITRATE);
	prm.profile_level_id = aac_profile;

	debug("aac: Encoder configuration: conf=%w, "
	     "frameLength=%u, inputChannels=%u\n",
	     enc_info.confBuf, enc_info.confSize,
	     enc_info.frameLength, enc_info.inputChannels);

	re_snprintf(prm.config, sizeof(prm.config), "%w",
			enc_info.confBuf, enc_info.confSize);

	aacEncClose(&enc);

	aac_encode_fmtp(&prm);

	debug("aac: fmtp=\"%s\"\n", fmtp_local);

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
