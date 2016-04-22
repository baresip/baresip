/**
 * @file mpa/decode.c mpa Decode
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <baresip.h>
#include <mpg123.h>
#include "mpa.h"


struct audec_state {
	mpg123_handle *dec;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;

	mpg123_close(ads->dec);
	mpg123_delete(ads->dec);
}


int mpa_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		       const char *fmtp)
{
	struct audec_state *ads;
	int mpaerr, err=0;

	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;

	mpg123_delete(ads->dec);
	ads->dec = mpg123_new(NULL,&mpaerr);
	if (!ads->dec) {
		warning("mpa: decoder create: %s\n", mpg123_plain_strerror(mpaerr));
		err = ENOMEM;
		goto out;
	}

	mpaerr = mpg123_param(ads->dec, MPG123_VERBOSE, 4, 4.);
	if(mpaerr != MPG123_OK) {
		error("MPA libmpg123 param error %s", mpg123_plain_strerror(mpaerr));
		return EINVAL;
	}


	mpaerr = mpg123_format(ads->dec, 48000 /*ac->srate*/, 2 /*ac->ch*/, MPG123_ENC_SIGNED_16);
	if(mpaerr != MPG123_OK) {
		error("MPA libmpg123 format error %s", mpg123_plain_strerror(mpaerr));
		return EINVAL;
	}

	mpaerr = mpg123_open_feed(ads->dec);
	if(mpaerr != MPG123_OK) {
		error("MPA libmpg123 open feed error %s", mpg123_plain_strerror(mpaerr));
		return EINVAL;
	}


 out:
	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


int mpa_decode_frm(struct audec_state *ads, int16_t *sampv, size_t *sampc,
		    const uint8_t *buf, size_t len)
{
	int mpaerr, channels, encoding;
	long samplerate;
	size_t n;
	uint32_t header;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	if(len<=4)
		return EINVAL;
	header = *(uint32_t*)buf;
	if(header != 0) {
		error("MPA header is not zero %08X\n", header);
		return EPROTO;
	}


	mpaerr = mpg123_decode(ads->dec, buf+4, len-4, (unsigned char*)sampv, *sampc*2, &n);
	if(mpaerr == MPG123_NEW_FORMAT) {
		mpg123_getformat(ads->dec, &samplerate, &channels, &encoding);
		info("MPA libmpg123 format change %d %d %04X\n",samplerate,channels,encoding);
	}
	else if(mpaerr == MPG123_NEED_MORE) 
		return 0;
	else if(mpaerr != MPG123_OK) { 
		error("MPA libmpg123 feed error %d %s", mpaerr, mpg123_plain_strerror(mpaerr));
		return EPROTO;
	}

//	warning("mpa decode %d %d %d\n",*sampc,len,n);
	*sampc = n / 2;

	return 0;
}

int mpa_decode_pkloss(struct audec_state *ads, int16_t *sampv, size_t *sampc)
{
	if (!ads || !sampv || !sampc)
		return EINVAL;

	warning("mpa packet loss %d\n",*sampc);
//	n = opus_decode(ads->dec, NULL, 0, sampv, (int)(*sampc/ads->ch), 0);
//	if (n < 0)
//		return EPROTO;

//	*sampc = n * ads->ch;

	return 0;
}


