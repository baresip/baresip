/**
 * @file mpa/decode.c mpa Decode
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <baresip.h>
#include <mpg123.h>
#include <speex/speex_resampler.h>
#include <string.h>
#include "mpa.h"

#undef DEBUG

struct audec_state {
	mpg123_handle *dec;
	SpeexResamplerState *resampler;
	int channels;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;

	mpg123_close(ads->dec);
	mpg123_delete(ads->dec);
#ifdef DEBUG
	debug("mpa: decoder destroyed\n");
#endif
}


int mpa_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		       const char *fmtp)
{
	struct audec_state *ads;
	int mpaerr, err=0;

	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

#ifdef DEBUG
	debug("mpa: decoder created %s\n",fmtp);
#endif

	if (ads)
		mem_deref(ads);
		
	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;
	ads->channels = 0;
	ads->resampler = NULL;

	ads->dec = mpg123_new(NULL,&mpaerr);
	if (!ads->dec) {
		error("mpa: decoder create: %s\n", mpg123_plain_strerror(mpaerr));
		err = ENOMEM;
		goto out;
	}

#ifdef DEBUG
	mpaerr = mpg123_param(ads->dec, MPG123_VERBOSE, 4, 4.);
#else
	mpaerr = mpg123_param(ads->dec, MPG123_VERBOSE, 0, 0.);
#endif
	if(mpaerr != MPG123_OK) {
		error("MPA libmpg123 param error %s", mpg123_plain_strerror(mpaerr));
		err = EINVAL;
		goto out;
	}


	mpaerr = mpg123_format_all(ads->dec);
	if(mpaerr != MPG123_OK) {
		error("MPA libmpg123 format error %s", mpg123_plain_strerror(mpaerr));
		err = EINVAL;
		goto out;
	}

	mpaerr = mpg123_open_feed(ads->dec);
	if(mpaerr != MPG123_OK) {
		error("MPA libmpg123 open feed error %s", mpg123_plain_strerror(mpaerr));
		err = EINVAL;
		goto out;
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
	int mpaerr, channels, encoding, i;
	long samplerate,res;
	size_t n;
	uint32_t header;
	static int16_t ds[2304];
	spx_uint32_t ds_len;
	spx_uint32_t in_len;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	if(len<=4)
		return EINVAL;
	header = *(uint32_t*)buf;
	if(header != 0) {
		error("MPA header is not zero %08X\n", header);
		return EPROTO;
	}

	if(ads->resampler)  {
		in_len = *sampc;
		ds_len = 2304*2;
		mpaerr = mpg123_decode(ads->dec, buf+4, len-4, (unsigned char*)ds, sizeof(ds), &n); /* n counts bytes */
		ds_len = n / 4;		/* ds_len counts samples per channel */
		res=speex_resampler_process_interleaved_int(ads->resampler, ds, &ds_len, sampv, &in_len);
		if (res!=RESAMPLER_ERR_SUCCESS) {
			error("mpa: upsample error: %s %d %d\n", strerror(res), in_len, *sampc/2);
			return EPROTO;
		}
#ifdef DEBUG
		debug("mpa decode %d %d %d %d\n",ds_len,*sampc,in_len,n);
#endif
		*sampc = in_len * 2;
	}
	else {
		mpaerr = mpg123_decode(ads->dec, buf+4, len-4, (unsigned char*)sampv, *sampc*2, &n);
#ifdef DEBUG
		debug("mpa decode %d %d\n",*sampc,n);
#endif
		*sampc = n / 2;
	}
	if(ads->channels==1) {
		for(i=*sampc-1;i>=0;i--)
			sampv[i+i+1]=sampv[i+i]=sampv[i];
		*sampc *= 2;
	}

	if(mpaerr == MPG123_NEW_FORMAT) {
		mpg123_getformat(ads->dec, &samplerate, &channels, &encoding);
		info("MPA libmpg123 format change %d %d %04X\n",samplerate,channels,encoding);

		ads->channels = channels;
			
		if(samplerate != 48000) {
			ads->resampler = speex_resampler_init(channels, samplerate, 48000, 3, &mpaerr);
			if(mpaerr!=RESAMPLER_ERR_SUCCESS || ads->resampler==NULL) {
				error("mpa: upsampler init failed %d\n",mpaerr);
				return EINVAL;
			}
		}
		else
			ads->resampler = NULL;

	}
	else if(mpaerr == MPG123_NEED_MORE) 
		return 0;
	else if(mpaerr != MPG123_OK) { 
		error("MPA libmpg123 feed error %d %s", mpaerr, mpg123_plain_strerror(mpaerr));
		return EPROTO;
	}

#ifdef DEBUG
	debug("mpa decode %d %d %d\n",*sampc,len,n);
#endif
	return 0;
}

