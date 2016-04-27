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

struct audec_state {
	mpg123_handle *dec;
	SpeexResamplerState *resampler;
	int channels;
	int16_t intermediate_buffer[MPA_FRAMESIZE*2];
	int start;
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
	int result, err=0;
	(void)fmtp;

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
	ads->start = 0;

	ads->dec = mpg123_new(NULL,&result);
	if (!ads->dec) {
		error("mpa: decoder create: %s\n",
			mpg123_plain_strerror(result));
		err = ENOMEM;
		goto out;
	}

#ifdef DEBUG
	result = mpg123_param(ads->dec, MPG123_VERBOSE, 4, 4.);
#else
	result = mpg123_param(ads->dec, MPG123_VERBOSE, 0, 0.);
#endif
	if (result != MPG123_OK) {
		error("MPA libmpg123 param error %s",
			mpg123_plain_strerror(result));
		err = EINVAL;
		goto out;
	}


	result = mpg123_format_all(ads->dec);
	if (result != MPG123_OK) {
		error("MPA libmpg123 format error %s",
			mpg123_plain_strerror(result));
		err = EINVAL;
		goto out;
	}

	result = mpg123_open_feed(ads->dec);
	if (result != MPG123_OK) {
		error("MPA libmpg123 open feed error %s",
			mpg123_plain_strerror(result));
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
	int result, channels, encoding, i;
	long samplerate;
	size_t n;
	spx_uint32_t intermediate_len;
	spx_uint32_t out_len;
	uint32_t header;

	if (!ads || !sampv || !sampc || !buf || len<=4)
		return EINVAL;

	header = *(uint32_t*)(void *)buf;
	if (header != 0) {
		error("MPA header is not zero %08X, not supported yet\n",
		      header);
		return EPROTO;
	}

	if (ads->resampler)  {
		result = mpg123_decode(ads->dec, buf+4, len-4,
				(unsigned char*)ads->intermediate_buffer,
				sizeof(ads->intermediate_buffer), &n);
				/* n counts bytes */
		intermediate_len = (uint32_t)(n / 2 / ads->channels);
			/* intermediate_len counts samples per channel */
		out_len = (uint32_t)*sampc;
		result=speex_resampler_process_interleaved_int(
			ads->resampler, ads->intermediate_buffer,
			&intermediate_len, sampv, &out_len);
		if (result!=RESAMPLER_ERR_SUCCESS) {
			error("mpa: upsample error: %s %d %d\n",
				strerror(result), out_len, *sampc/2);
			return EPROTO;
		}
#ifdef DEBUG
		info("mpa decode %d %d %d %d\n",intermediate_len,*sampc,
			out_len,n);
#endif
		*sampc = out_len * ads->channels;
	}
	else {
		result = mpg123_decode(ads->dec, buf+4, len-4,
				(unsigned char*)sampv, *sampc*2, &n);
#ifdef DEBUG
		info("mpa decode %d %d\n",*sampc,n);
#endif
		*sampc = n / 2;
	}

	if (ads->start<100) {	/* mpg123 needs some to sync */
		ads->start++;
		*sampc=0;
	}
	if (ads->channels==1) {
		for (i=(int)(*sampc-1); i>=0; i--)
			sampv[i+i+1]=sampv[i+i]=sampv[i];
		*sampc *= 2;
	}

	if (result == MPG123_NEW_FORMAT) {
		mpg123_getformat(ads->dec, &samplerate, &channels, &encoding);
		info("MPA libmpg123 format change %d %d %04X\n",samplerate
			,channels,encoding);

		ads->channels = channels;
		ads->start = 0;
		if (samplerate != 48000) {
			ads->resampler = speex_resampler_init(channels,
				      (uint32_t)samplerate, 48000, 3, &result);
			if (result!=RESAMPLER_ERR_SUCCESS
				|| ads->resampler==NULL) {
				error("mpa: upsampler failed %d\n",result);
				return EINVAL;
			}
		}
		else
			ads->resampler = NULL;
	}
	else if (result == MPG123_NEED_MORE)
		return 0;
	else if (result != MPG123_OK) {
		error("MPA libmpg123 feed error %d %s", result,
			mpg123_plain_strerror(result));
		return EPROTO;
	}

#ifdef DEBUG
	debug("mpa decode %d %d %d\n",*sampc,len,n);
#endif
	return 0;
}

