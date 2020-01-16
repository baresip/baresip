/**
 * @file opus_multistream/encode.c Opus Multistream Encode
 *
 * Copyright (C) 2019 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <opus/opus_multistream.h>
#include "opus_multistream.h"


struct auenc_state {
	OpusMSEncoder *enc;
	unsigned ch;
};


static void destructor(void *arg)
{
	struct auenc_state *aes = arg;

	if (aes->enc)
		opus_multistream_encoder_destroy(aes->enc);
}


static opus_int32 srate2bw(opus_int32 srate)
{
	if (srate >= 48000)
		return OPUS_BANDWIDTH_FULLBAND;
	else if (srate >= 24000)
		return OPUS_BANDWIDTH_SUPERWIDEBAND;
	else if (srate >= 16000)
		return OPUS_BANDWIDTH_WIDEBAND;
	else if (srate >= 12000)
		return OPUS_BANDWIDTH_MEDIUMBAND;
	else
		return OPUS_BANDWIDTH_NARROWBAND;
}


#if 0
static const char *bwname(opus_int32 bw)
{
	switch (bw) {
	case OPUS_BANDWIDTH_FULLBAND:      return "full";
	case OPUS_BANDWIDTH_SUPERWIDEBAND: return "superwide";
	case OPUS_BANDWIDTH_WIDEBAND:      return "wide";
	case OPUS_BANDWIDTH_MEDIUMBAND:    return "medium";
	case OPUS_BANDWIDTH_NARROWBAND:    return "narrow";
	default:                           return "???";
	}
}


static const char *chname(opus_int32 ch)
{
	switch (ch) {
	case OPUS_AUTO: return "auto";
	case 1:         return "mono";
	case 2:         return "stereo";
	default:        return "???";
	}
}
#endif


int opus_multistream_encode_update(struct auenc_state **aesp,
				   const struct aucodec *ac,
		       struct auenc_param *param, const char *fmtp)
{
	struct auenc_state *aes;
	unsigned ch;
	unsigned char mapping[256];

	struct opus_multistream_param prm, conf_prm;
	opus_int32 fch, vbr;
	const struct aucodec *auc = ac;

	(void)param;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	debug("opus_multistream: encoder fmtp (%s)\n", fmtp);

	/* Save the incoming OPUS parameters from SDP offer */
	if (str_isset(fmtp)) {
		opus_multistream_mirror_params(fmtp);
	}

	/* create one mapping per channel */
	for (ch=0; ch<ac->ch; ch++) {
		if (ch >= 256) {
			warning("opus: Exceeding the acceptable"
				" 255 channel-mappings");
			return EINVAL;
		}
		else {
			mapping[ch] = ch;
		}
	}

	aes = *aesp;

	if (!aes) {
		const opus_int32 complex = opus_ms_complexity;
		int opuserr;

		aes = mem_zalloc(sizeof(*aes), destructor);
		if (!aes)
			return ENOMEM;

		aes->ch = ac->ch;


		aes->enc = opus_multistream_encoder_create(ac->srate, ac->ch,
							   opus_ms_streams,
							   opus_ms_c_streams,
							   mapping,
							   opus_ms_application,
							   &opuserr);
		if (!aes->enc) {
			warning("opus_multistream: encoder create: %s\n",
				opus_strerror(opuserr));
			mem_deref(aes);
			return ENOMEM;
		}

		(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_SET_COMPLEXITY(complex));

		*aesp = aes;
	}

	prm.srate      = 48000;
	prm.bitrate    = OPUS_AUTO;
	prm.stereo     = 1;
	prm.cbr        = 0;
	prm.inband_fec = 0;
	prm.dtx        = 0;

	opus_multistream_decode_fmtp(&prm, fmtp);

	conf_prm.bitrate = OPUS_AUTO;
	opus_multistream_decode_fmtp(&conf_prm, auc->fmtp);

	if ((prm.bitrate == OPUS_AUTO) ||
	    ((conf_prm.bitrate != OPUS_AUTO) &&
	     (conf_prm.bitrate < prm.bitrate)))
		prm.bitrate = conf_prm.bitrate;

	fch = prm.stereo ? OPUS_AUTO : 1;
	vbr = prm.cbr ? 0 : 1;

	/* override local bitrate */
	if (param && param->bitrate)
		prm.bitrate = param->bitrate;

	(void)opus_multistream_encoder_ctl(aes->enc,
			       OPUS_SET_MAX_BANDWIDTH(srate2bw(prm.srate)));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_SET_BITRATE(prm.bitrate));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_SET_FORCE_CHANNELS(fch));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_SET_VBR(vbr));
	(void)opus_multistream_encoder_ctl(aes->enc,
				   OPUS_SET_INBAND_FEC(prm.inband_fec));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_SET_DTX(prm.dtx));


#if 0
	{
	opus_int32 bw, complex;

	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_GET_MAX_BANDWIDTH(&bw));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_GET_BITRATE(&prm.bitrate));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_GET_FORCE_CHANNELS(&fch));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_GET_VBR(&vbr));
	(void)opus_multistream_encoder_ctl(aes->enc,
				   OPUS_GET_INBAND_FEC(&prm.inband_fec));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_GET_DTX(&prm.dtx));
	(void)opus_multistream_encoder_ctl(aes->enc,
					   OPUS_GET_COMPLEXITY(&complex));

	debug("opus_multistream: encode bw=%s bitrate=%i fch=%s "
	      "vbr=%i fec=%i dtx=%i complex=%i\n",
	      bwname(bw), prm.bitrate, chname(fch),
	      vbr, prm.inband_fec, prm.dtx, complex);
	}
#endif

	return 0;
}


int opus_multistream_encode_frm(struct auenc_state *aes,
				bool *marker, uint8_t *buf, size_t *len,
				int fmt, const void *sampv, size_t sampc)
{
	opus_int32 n;
	(void)marker;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	switch (fmt) {

	case AUFMT_S16LE:
		n = opus_multistream_encode(aes->enc,
					    sampv, (int)(sampc/aes->ch),
				buf, (opus_int32)(*len));
		if (n < 0) {
			warning("opus_multistream: encode error: %s\n",
				opus_strerror((int)n));
			return EPROTO;
		}
		break;

	case AUFMT_FLOAT:
		n = opus_multistream_encode_float(aes->enc,
						  sampv, (int)(sampc/aes->ch),
				      buf, (opus_int32)(*len));
		if (n < 0) {
			warning("opus_multistream: float encode error: %s\n",
				opus_strerror((int)n));
			return EPROTO;
		}
		break;

	default:
		return ENOTSUP;
	}

	*len = n;

	return 0;
}
