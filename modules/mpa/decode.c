/**
 * @file mpa/decode.c mpa Decode
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <rem.h>
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

	if (ads->resampler)
		speex_resampler_destroy(ads->resampler);

	mpg123_close(ads->dec);
	mpg123_delete(ads->dec);
#ifdef DEBUG
	debug("MPA dec destroyed\n");
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
	debug("MPA dec created %s\n",fmtp);
#endif

	if (!ads) {
		ads = mem_zalloc(sizeof(*ads), destructor);
		if (!ads)
			return ENOMEM;
	}
	else {
		memset(ads,0,sizeof(*ads));
	}
	ads->channels = 0;
	ads->resampler = NULL;
	ads->start = 0;

	ads->dec = mpg123_new(NULL,&result);
	if (!ads->dec) {
		warning("MPA dec create: %s\n",
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
		warning("MPA dec param error %s\n",
			mpg123_plain_strerror(result));
		err = EINVAL;
		goto out;
	}


	result = mpg123_format_all(ads->dec);
	if (result != MPG123_OK) {
		warning("MPA dec format error %s\n",
			mpg123_plain_strerror(result));
		err = EINVAL;
		goto out;
	}

	result = mpg123_open_feed(ads->dec);
	if (result != MPG123_OK) {
		warning("MPA dec open feed error %s\n",
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


int mpa_decode_frm(struct audec_state *ads,
		   int fmt, void *sampv_void, size_t *sampc,
		   bool marker, const uint8_t *buf, size_t len)
{
	int result, channels, encoding, i;
	long samplerate;
	size_t n;
	spx_uint32_t intermediate_len;
	spx_uint32_t out_len;
	int16_t *sampv = sampv_void;
	(void)marker;

#ifdef DEBUG
	debug("MPA dec start %d %ld\n",len, *sampc);
#endif

	if (!ads || !sampv || !sampc || !buf || len<=4)
		return EINVAL;

	if (*(uint32_t*)(void *)buf != 0) {
		warning("MPA dec header is not zero %08X, not supported yet\n",
			*(uint32_t*)(void *)buf);
		return EPROTO;
	}

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	n = 0;
	result = mpg123_decode(ads->dec, buf+4, len-4,
				(unsigned char*)ads->intermediate_buffer,
				sizeof(ads->intermediate_buffer), &n);
				/* n counts bytes */
#ifdef DEBUG
	debug("MPA dec %d %d %d %d\n",result, len-4, n, ads->channels);
#endif

	if (result == MPG123_NEW_FORMAT) {
		mpg123_getformat(ads->dec, &samplerate, &channels, &encoding);
		info("MPA dec format change %d %d %04X\n",samplerate
			,channels,encoding);

		ads->channels = channels;
		ads->start = 0;
		if (ads->resampler)
			speex_resampler_destroy(ads->resampler);
		if (samplerate != MPA_IORATE) {
			ads->resampler = speex_resampler_init(channels,
				      (uint32_t)samplerate, MPA_IORATE,
				      3, &result);
			if (result!=RESAMPLER_ERR_SUCCESS
				|| ads->resampler==NULL) {
				warning("MPA dec upsampler failed %d\n",
					result);
				return EINVAL;
			}
		}
		else
			ads->resampler = NULL;
	}
	else if (result == MPG123_NEED_MORE)
		;			/* workaround: do nothing */
	else if (result != MPG123_OK) {
		warning("MPA dec feed error %d %s\n", result,
			mpg123_plain_strerror(result));
		return EPROTO;
	}

	if (ads->resampler)  {
		intermediate_len = (uint32_t)(n / 2 / ads->channels);
			/* intermediate_len counts samples per channel */
		out_len = (uint32_t)(*sampc / 2);

		result=speex_resampler_process_interleaved_int(
			ads->resampler, ads->intermediate_buffer,
			&intermediate_len, sampv, &out_len);
		if (result!=RESAMPLER_ERR_SUCCESS) {
			warning("MPA dec upsample error: %s %d %d\n",
				strerror(result), out_len, *sampc/2);
			return EPROTO;
		}
		if (ads->channels==1) {
			for (i=out_len-1;i>=0;i--)
				sampv[i+i+1]=sampv[i+i]=sampv[i];
			*sampc = out_len * 2;
		}
		else
			*sampc = out_len * ads->channels;
	}
	else {
		n /= 2;
		if (ads->channels!=1) {
			for (i=0;(unsigned)i<n;i++)
				sampv[i]=ads->intermediate_buffer[i];
			*sampc = n;
		}
		else {
			for (i=0;(unsigned)i<n;i++)
				sampv[i*2]=sampv[i*2+1]=
					ads->intermediate_buffer[i];
			*sampc = n * 2;
	}

#ifdef DEBUG
	debug("MPA dec done %d\n",*sampc);
#endif
	}

	return 0;
}

