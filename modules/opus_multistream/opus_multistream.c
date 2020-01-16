/**
 * @file opus_multistream.c Opus Multistream Audio Codec
 *
 * Copyright (C) 2019 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include <opus/opus_multistream.h>
#include "opus_multistream.h"


/**
 * @defgroup opus opus
 *
 * The OPUS multistream audio codec
 *
 * Supported version: libopus 1.0.0 or later
 *
 * Configuration options:
 *
 \verbatim
  opus_stereo        yes     # Request peer to send stereo
  opus_sprop_stereo  yes     # Sending stereo
  opus_bitrate    128000     # Average bitrate in [bps]
  opus_cbr        {yes,no}   # Constant Bitrate (inverse of VBR)
  opus_inbandfec  {yes,no}   # Enable inband Forward Error Correction (FEC)
  opus_dtx        {yes,no}   # Enable Discontinuous Transmission (DTX)
  opus_complexity {0-10}     # Encoder's computational complexity (10 max)
  opus_application {audio, voip} # Encoder's intended application
 \endverbatim
 *
 * References:
 *
 *    RFC 6716  Definition of the Opus Audio Codec
 *    RFC 7587  RTP Payload Format for the Opus Speech and Audio Codec
 *
 *    http://opus-codec.org/downloads/
 */


static bool opus_mirror;
static char fmtp[256] = "";
static char fmtp_mirror[256];

uint32_t opus_ms_complexity = 10;
opus_int32 opus_ms_application = OPUS_APPLICATION_AUDIO;

uint32_t opus_ms_channels = 2;
uint32_t opus_ms_streams = 2;
uint32_t opus_ms_c_streams = 2;


static int opus_multistream_fmtp_enc(struct mbuf *mb,
				     const struct sdp_format *fmt,
			 bool offer, void *arg)
{
	bool mirror;

	(void)arg;
	(void)offer;

	if (!mb || !fmt)
		return 0;

	mirror = !offer && str_isset(fmtp_mirror);

	return mbuf_printf(mb, "a=fmtp:%s %s\r\n",
			   fmt->id, mirror ? fmtp_mirror : fmtp);
}


static struct aucodec opus_multistream = {
	.name      = "opus_multistream",    /* NOTE: not standard */
	.srate     = 48000,
	.crate     = 48000,
	.ch        = 2,                     /* NOTE: configurable */
	.pch       = 2,
	.fmtp      = fmtp,
	.encupdh   = opus_multistream_encode_update,
	.ench      = opus_multistream_encode_frm,
	.decupdh   = opus_multistream_decode_update,
	.dech      = opus_multistream_decode_frm,
	.plch      = opus_multistream_decode_pkloss,
};


void opus_multistream_mirror_params(const char *x)
{
	if (!opus_mirror)
		return;

	info("opus_multistream: mirror parameters: \"%s\"\n", x);

	str_ncpy(fmtp_mirror, x, sizeof(fmtp_mirror));
}


static int module_init(void)
{
	struct conf *conf = conf_cur();
	uint32_t value;
	char *p = fmtp + str_len(fmtp);
	bool b, stereo = true, sprop_stereo = true;
	struct pl pl;
	int n = 0;

	conf_get_bool(conf, "opus_stereo", &stereo);
	conf_get_bool(conf, "opus_sprop_stereo", &sprop_stereo);

	/* always set stereo parameter first */
	n = re_snprintf(p, sizeof(fmtp) - str_len(p),
			"stereo=%d;sprop-stereo=%d", stereo, sprop_stereo);
	if (n <= 0)
		return ENOMEM;

	p += n;

	if (0 == conf_get_u32(conf, "opus_bitrate", &value)) {

		n = re_snprintf(p, sizeof(fmtp) - str_len(p),
				";maxaveragebitrate=%d", value);
		if (n <= 0)
			return ENOMEM;

		p += n;
	}

	if (0 == conf_get_bool(conf, "opus_cbr", &b)) {

		n = re_snprintf(p, sizeof(fmtp) - str_len(p),
				";cbr=%d", b);
		if (n <= 0)
			return ENOMEM;

		p += n;
	}

	if (0 == conf_get_bool(conf, "opus_inbandfec", &b)) {

		n = re_snprintf(p, sizeof(fmtp) - str_len(p),
				";useinbandfec=%d", b);
		if (n <= 0)
			return ENOMEM;

		p += n;
	}

	if (0 == conf_get_bool(conf, "opus_dtx", &b)) {

		n = re_snprintf(p, sizeof(fmtp) - str_len(p),
				";usedtx=%d", b);
		if (n <= 0)
			return ENOMEM;

		p += n;
	}

	(void)conf_get_bool(conf, "opus_mirror", &opus_mirror);

	if (opus_mirror) {
		opus_multistream.fmtp = NULL;
		opus_multistream.fmtp_ench = opus_multistream_fmtp_enc;
	}

	(void)conf_get_u32(conf, "opus_complexity", &opus_ms_complexity);

	if (opus_ms_complexity > 10)
		opus_ms_complexity = 10;

	if (!conf_get(conf, "opus_application", &pl)) {
		if (!pl_strcasecmp(&pl, "audio"))
			opus_ms_application = OPUS_APPLICATION_AUDIO;
		else if (!pl_strcasecmp(&pl, "voip"))
			opus_ms_application = OPUS_APPLICATION_VOIP;
		else {
			warning("opus: unknown encoder application: %r\n",
					&pl);
			return EINVAL;
		}
	}

	(void)conf_get_u32(conf, "opus_ms_channels", &opus_ms_channels);

	opus_multistream.ch = opus_ms_channels;

	(void)conf_get_u32(conf, "opus_ms_streams", &opus_ms_streams);
	(void)conf_get_u32(conf, "opus_ms_c_streams", &opus_ms_c_streams);

	debug("opus_multistream: fmtp=\"%s\"\n", fmtp);

	aucodec_register(baresip_aucodecl(), &opus_multistream);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&opus_multistream);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(opus_multistream) = {
	"opus_multistream",
	"audio codec",
	module_init,
	module_close,
};
