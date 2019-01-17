/**
 * @file mpa.c mpa Audio Codec
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <baresip.h>
#include <ctype.h>
#include <string.h>
#include "mpa.h"
#include <mpg123.h>

/**
 * @defgroup mpa mpa
 *
 * The mpa audio codec
 *
 * Supported version:
 *      libmpg123   1.16.0 or later
 *      libtwolame  0.3.13 or later
 *
 * References:
 *
 *    RFC 2250  RTP Payload Format for the mpa Speech and Audio Codec
 *
 */

/*
4.1.17. Registration of MIME media type audio/MPA

   MIME media type name: audio

   MIME subtype name: MPA (MPEG audio)

   Required parameters: None

   Optional parameters:
       layer: which layer of MPEG audio encoding; permissible values
       are 1, 2, 3.

       samplerate: the rate at which audio is sampled.  MPEG-1 audio
       supports sampling rates of 32, 44.1, and 48 kHz; MPEG-2
       supports sampling rates of 16, 22.05 and 24 kHz.  This parameter
       is separate from the RTP timestamp clock rate which is always
       90000 Hz for MPA.

       mode: permissible values are "stereo", "joint_stereo",
       "single_channel", "dual_channel".  The "channels" parameter
       does not apply to MPA.  It is undefined to put a number of
       channels in the SDP rtpmap attribute for MPA.

       bitrate: the data rate for the audio bit stream.

       ptime: RECOMMENDED duration of each packet in milliseconds.

       maxptime: maximum duration of each packet in milliseconds.

       Parameters which are omitted are left to the encoder to choose
       based on the session bandwidth, configuration information, or
       other constraints.  The selected layer as well as the sampling
       rate and mode are indicated in the payload so receivers can
       process the data without these parameters being specified
       externally.

   Encoding considerations:
       This type is only defined for transfer via RTP [RFC 3550].

   Security considerations: See Section 5 of RFC 3555

   Interoperability considerations: none

   Published specification: RFC 3551

   Applications which use this media type:
       Audio and video streaming and conferencing tools.

*/


static struct aucodec mpa = {
	.pt       = "14",
	.name      = "MPA",
	.srate     = MPA_IORATE,
	.crate     = MPA_RTPRATE,
	.ch        = 2,
	.pch       = 1,
/* MPA does not expect channels count, even those it is stereo */
	.fmtp      = "layer=2",
	.encupdh   = mpa_encode_update,
	.ench      = mpa_encode_frm,
	.decupdh   = mpa_decode_update,
	.dech      = mpa_decode_frm,
};


static int module_init(void)
{
	struct conf *conf = conf_cur();
	uint32_t value;
	static char fmtp[256];
	static char mode[30];
	int res;

	/** generate fmtp string based on config file */

	strcpy(mode,mpa.fmtp);

	if (0 == conf_get_u32(conf, "mpa_bitrate", &value)) {
		if (value<8000 || value>384000) {
			warning("MPA bitrate between 8000 and "
				"384000 are allowed.\n");
			return -1;
		}

		(void)re_snprintf(fmtp+strlen(fmtp),
			sizeof(fmtp)-strlen(fmtp),
			"; bitrate=%d", value);
	}
	if (0 == conf_get_u32(conf, "mpa_layer", &value)) {
		if (value<1 || value>4) {
			warning("MPA layer 1, 2 or 3 are allowed.");
			return -1;
		}
		(void)re_snprintf(fmtp+strlen(fmtp),
			sizeof(fmtp)-strlen(fmtp),
			"; layer=%d", value);
	}
	if (0 == conf_get_u32(conf, "mpa_samplerate", &value)) {
		switch (value) {
		case 32000:
		case 44100:
		case 48000:
		case 16000:
		case 22050:
		case 24000:
			break;
		default:
			warning("MPA samplerates of 16, 22.05, 24, 32, "
				"44.1, and 48 kHz are allowed.\n");
			return -1;
		}
		(void)re_snprintf(fmtp+strlen(fmtp),
			sizeof(fmtp)-strlen(fmtp),
			"; samplerate=%d", value);
	}
	if (0 == conf_get_str(conf, "mpa_mode", mode, sizeof(mode))) {
		char *p = mode;
		while (*p) {
			*p = tolower(*p);
			++p;
		}

		if (strcmp(mode,"stereo")
			&& strcmp(mode,"joint_stereo")
			&& strcmp(mode,"single_channel")
			&& strcmp(mode,"dual_channel")) {
			warning("MPA mode: Permissible values are stereo, "
			    "joint_stereo, single_channel, dual_channel.\n");
			return -1;
		}

		(void)re_snprintf(fmtp+strlen(fmtp),
			sizeof(fmtp)-strlen(fmtp),
			"; mode=%s", mode);
	}

	if (fmtp[0]==';' && fmtp[1]==' ')
		mpa.fmtp = fmtp+2;
	else
		mpa.fmtp = fmtp;

	/* init decoder library */
	res = mpg123_init();
	if (res != MPG123_OK) {
		warning("MPA libmpg123 init error %s\n",
			mpg123_plain_strerror(res));
		return -1;
	}

	aucodec_register(baresip_aucodecl(), &mpa);

#ifdef DEBUG
	info("MPA init with %s\n",mpa.fmtp);
#endif
	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&mpa);

	mpg123_exit();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(mpa) = {
	"MPA",
	"audio codec",
	module_init,
	module_close,
};

