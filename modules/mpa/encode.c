/**
 * @file mpa/encode.c mpa Encode
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <lame/lame.h>
#include <string.h>
#include <speex/speex_resampler.h>
#include "mpa.h"


struct auenc_state {
	lame_global_flags *enc;
	int channels, samplerate;
	SpeexResamplerState *resampler;
	int16_t intermediate_buffer[MPA_FRAMESIZE*6];
};


static void destructor(void *arg)
{
	struct auenc_state *aes = arg;

	if (aes->resampler) {
		speex_resampler_destroy(aes->resampler);
		aes->resampler = NULL;
	}

	if (aes->enc)
		lame_close(aes->enc);
#ifdef DEBUG
	debug("MPA enc destroyed\n");
#endif
}


int mpa_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		       struct auenc_param *param, const char *fmtp)
{
	struct auenc_state *aes;
	struct mpa_param prm;
	int result,err=0;

	(void)param;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	aes = *aesp;
	if (!aes) {
		aes = mem_zalloc(sizeof(*aes), destructor);
		if (!aes)
			return ENOMEM;

	}
	else
		memset(aes,0,sizeof(*aes));

	aes->enc = lame_init();
	if (!aes->enc) {
		warning("MPA enc create failed\n");
		mem_deref(aes);
		return ENOMEM;
	}
#ifdef DEBUG
	debug("MPA enc created %s\n",fmtp);
#endif
	aes->channels = ac->ch;

	prm.samplerate = 48000;
	prm.bitrate    = 128000;
	prm.layer      = 2;
	prm.mode       = MONO;
	mpa_decode_fmtp(&prm, fmtp);
	aes->samplerate = prm.samplerate;

	result = 0;

	result |= lame_set_mode(aes->enc, prm.mode);
	result |= lame_set_brate(aes->enc, prm.bitrate/1000);
	result |= lame_set_in_samplerate(aes->enc, prm.samplerate);
	result |= lame_set_out_samplerate(aes->enc, prm.samplerate);
	result |= lame_set_num_channels(aes->enc, 2);
	if (result!=0) {
		warning("MPA enc set failed\n");
		err=EINVAL;
		goto out;
	}

	result = lame_init_params(aes->enc);
	if (result!=0) {
		warning("MPA enc init params failed\n");
		err=EINVAL;
		goto out;
	}
#ifdef DEBUG
	lame_print_config(aes->enc);
#endif
	if (prm.samplerate != MPA_IORATE) {
		aes->resampler = speex_resampler_init(2, MPA_IORATE,
			prm.samplerate, 3, &result);
		if (result!=RESAMPLER_ERR_SUCCESS) {
			warning("MPA enc resampler init failed %d\n",result);
			err=EINVAL;
			goto out;
		}

	}
	else
		aes->resampler = NULL;

out:
	if (err)
		mem_deref(aes);
	else
		*aesp = aes;

	return err;
}


int mpa_encode_frm(struct auenc_state *aes, uint8_t *buf, size_t *len,
		   int fmt, const void *sampv, size_t sampc)
{
	int n;
	spx_uint32_t intermediate_len,in_len;
	uint32_t ts_delta = 0;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (aes->resampler)  {
		in_len = (uint32_t)sampc/2;
		intermediate_len = sizeof(aes->intermediate_buffer)
			/ sizeof(aes->intermediate_buffer[0]);
		n=speex_resampler_process_interleaved_int(aes->resampler,
			sampv, &in_len, aes->intermediate_buffer,
			&intermediate_len);
		if (n!=RESAMPLER_ERR_SUCCESS || in_len != sampc/2) {
			warning("MPA enc downsample error: %s %d %d\n",
				strerror(n), in_len, sampc/2);
			return EPROTO;
		}
		n = lame_encode_buffer_interleaved(aes->enc,
			aes->intermediate_buffer, intermediate_len,
			buf+4, (int)(*len)-4);
#ifdef DEBUG
		debug("MPA enc %d %d %d %d %d %p\n",intermediate_len,sampc,
			aes->channels,*len,n,aes->enc);
#endif
	}
	else {
		n = lame_encode_buffer_interleaved(aes->enc,
				   (int16_t *)sampv, (int)(sampc/2),
				   buf+4, (int)(*len)-4);
#ifdef DEBUG
		debug("MPA enc %d %d %d %d\n",sampc,
			aes->channels,*len,n);
#endif
	}
	if (n < 0) {
		warning("MPA enc error %s\n", strerror((int)n));
		return EPROTO;
	}

	if (n > 0) {
		*(uint32_t*)(void *)buf = 0;
		*len = n+4;

		ts_delta = ((MPA_FRAMESIZE*MPA_RTPRATE)<<4) / aes->samplerate;
	}
	else
		*len = 0;

#ifdef DEBUG
	debug("MPA enc done %d %d %d %d %p\n",sampc,aes->channels,
		*len,n,aes->enc);
#endif

	return 0x00010000 | ((ts_delta>>4) & 0x0000ffff);
}

