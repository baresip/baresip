/**
 * @file opus_multistream/decode.c Opus Multistream Decode
 *
 * Copyright (C) 2019 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <opus/opus_multistream.h>
#include "opus_multistream.h"


struct audec_state {
	OpusMSDecoder *dec;
	unsigned ch;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;

	if (ads->dec)
		opus_multistream_decoder_destroy(ads->dec);
}


int opus_multistream_decode_update(struct audec_state **adsp,
				   const struct aucodec *ac, const char *fmtp)
{
	struct audec_state *ads;
	unsigned ch;
	unsigned char mapping[256];

	int opuserr, err = 0;
	(void)fmtp;

	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	/* create one mapping per channel */
	for (ch=0; ch<ac->ch; ch++) {

		if (ch >= 256) {
			warning("opus: Exceeding the acceptable"
				" 255 channel-mappings\n");
			return EINVAL;
		}
		else {
			mapping[ch] = ch;
		}
	}

	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;

	ads->ch = ac->ch;

	ads->dec = opus_multistream_decoder_create(ac->srate, ac->ch,
						   opus_ms_streams,
						   opus_ms_c_streams,
						   mapping, &opuserr);
	if (!ads->dec) {
		warning("opus_multistream: decoder create: %s\n",
			opus_strerror(opuserr));
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


int opus_multistream_decode_frm(struct audec_state *ads,
				int fmt, void *sampv, size_t *sampc,
				bool marker, const uint8_t *buf, size_t len)
{
	int n;
	(void)marker;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	switch (fmt) {

	case AUFMT_S16LE:
		n = opus_multistream_decode(ads->dec, buf, (opus_int32)len,
				sampv, (int)(*sampc/ads->ch), 0);
		if (n < 0) {
			warning("opus_multistream: decode error: %s\n",
				opus_strerror(n));
			return EPROTO;
		}
		break;

	case AUFMT_FLOAT:
		n = opus_multistream_decode_float(ads->dec,
						  buf, (opus_int32)len,
				      sampv, (int)(*sampc/ads->ch), 0);
		if (n < 0) {
			warning("opus_multistream: float decode error: %s\n",
				opus_strerror(n));
			return EPROTO;
		}
		break;

	default:
		return ENOTSUP;
	}

	*sampc = n * ads->ch;

	return 0;
}


int opus_multistream_decode_pkloss(struct audec_state *ads,
				   int fmt, void *sampv, size_t *sampc,
				   const uint8_t *buf, size_t len)
{
	int n;
	(void)buf;
	(void)len;

	if (!ads || !sampv || !sampc)
		return EINVAL;

	switch (fmt) {

	case AUFMT_S16LE:
		n = opus_multistream_decode(ads->dec, NULL, 0,
				sampv, (int)(*sampc/ads->ch), 0);
		if (n < 0)
			return EPROTO;
		break;

	case AUFMT_FLOAT:
		n = opus_multistream_decode_float(ads->dec, NULL, 0,
				      sampv, (int)(*sampc/ads->ch), 0);
		if (n < 0)
			return EPROTO;
		break;

	default:
		return ENOTSUP;
	}

	*sampc = n * ads->ch;

	return 0;
}
