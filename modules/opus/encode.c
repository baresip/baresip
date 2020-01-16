/**
 * @file opus/encode.c Opus Encode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <opus/opus.h>
#include "opus.h"


struct auenc_state {
	OpusEncoder *enc;
	unsigned ch;
};


static void destructor(void *arg)
{
	struct auenc_state *aes = arg;

	if (aes->enc)
		opus_encoder_destroy(aes->enc);
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


int opus_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		       struct auenc_param *param, const char *fmtp)
{
	struct auenc_state *aes;
	struct opus_param prm, conf_prm;
	opus_int32 fch, vbr;
	const struct aucodec *auc = ac;

	(void)param;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	debug("opus: encoder fmtp (%s)\n", fmtp);

	/* Save the incoming OPUS parameters from SDP offer */
	if (str_isset(fmtp)) {
		opus_mirror_params(fmtp);
	}

	aes = *aesp;

	if (!aes) {
		const opus_int32 complex = opus_complexity;
		int opuserr;

		aes = mem_zalloc(sizeof(*aes), destructor);
		if (!aes)
			return ENOMEM;

		aes->ch = ac->ch;

		aes->enc = opus_encoder_create(ac->srate, ac->ch,
					       opus_application,
					       &opuserr);
		if (!aes->enc) {
			warning("opus: encoder create: %s\n",
				opus_strerror(opuserr));
			mem_deref(aes);
			return ENOMEM;
		}

		(void)opus_encoder_ctl(aes->enc, OPUS_SET_COMPLEXITY(complex));

		*aesp = aes;
	}

	prm.srate      = 48000;
	prm.bitrate    = OPUS_AUTO;
	prm.stereo     = 1;
	prm.cbr        = 0;
	prm.inband_fec = 0;
	prm.dtx        = 0;

	opus_decode_fmtp(&prm, fmtp);

	conf_prm.bitrate = OPUS_AUTO;
	opus_decode_fmtp(&conf_prm, auc->fmtp);

	if ((prm.bitrate == OPUS_AUTO) ||
	    ((conf_prm.bitrate != OPUS_AUTO) &&
	     (conf_prm.bitrate < prm.bitrate)))
		prm.bitrate = conf_prm.bitrate;

	fch = prm.stereo ? OPUS_AUTO : 1;
	vbr = prm.cbr ? 0 : 1;

	/* override local bitrate */
	if (param && param->bitrate)
		prm.bitrate = param->bitrate;

	(void)opus_encoder_ctl(aes->enc,
			       OPUS_SET_MAX_BANDWIDTH(srate2bw(prm.srate)));
	(void)opus_encoder_ctl(aes->enc, OPUS_SET_BITRATE(prm.bitrate));
	(void)opus_encoder_ctl(aes->enc, OPUS_SET_FORCE_CHANNELS(fch));
	(void)opus_encoder_ctl(aes->enc, OPUS_SET_VBR(vbr));
	(void)opus_encoder_ctl(aes->enc, OPUS_SET_INBAND_FEC(prm.inband_fec));
	(void)opus_encoder_ctl(aes->enc, OPUS_SET_DTX(prm.dtx));

	if (opus_packet_loss) {
		opus_encoder_ctl(aes->enc,
				 OPUS_SET_PACKET_LOSS_PERC(opus_packet_loss));
	}

#if 0
	{
	opus_int32 bw, complex;

	(void)opus_encoder_ctl(aes->enc, OPUS_GET_MAX_BANDWIDTH(&bw));
	(void)opus_encoder_ctl(aes->enc, OPUS_GET_BITRATE(&prm.bitrate));
	(void)opus_encoder_ctl(aes->enc, OPUS_GET_FORCE_CHANNELS(&fch));
	(void)opus_encoder_ctl(aes->enc, OPUS_GET_VBR(&vbr));
	(void)opus_encoder_ctl(aes->enc, OPUS_GET_INBAND_FEC(&prm.inband_fec));
	(void)opus_encoder_ctl(aes->enc, OPUS_GET_DTX(&prm.dtx));
	(void)opus_encoder_ctl(aes->enc, OPUS_GET_COMPLEXITY(&complex));

	debug("opus: encode bw=%s bitrate=%i fch=%s "
	      "vbr=%i fec=%i dtx=%i complex=%i\n",
	      bwname(bw), prm.bitrate, chname(fch),
	      vbr, prm.inband_fec, prm.dtx, complex);
	}
#endif

	return 0;
}


int opus_encode_frm(struct auenc_state *aes,
		    bool *marker, uint8_t *buf, size_t *len,
		    int fmt, const void *sampv, size_t sampc)
{
	opus_int32 n;
	(void)marker;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	switch (fmt) {

	case AUFMT_S16LE:
		n = opus_encode(aes->enc, sampv, (int)(sampc/aes->ch),
				buf, (opus_int32)(*len));
		if (n < 0) {
			warning("opus: encode error: %s\n",
				opus_strerror((int)n));
			return EPROTO;
		}
		break;

	case AUFMT_FLOAT:
		n = opus_encode_float(aes->enc, sampv, (int)(sampc/aes->ch),
				      buf, (opus_int32)(*len));
		if (n < 0) {
			warning("opus: float encode error: %s\n",
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
