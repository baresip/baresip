/**
 * @file mpa/encode.c mpa Encode
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <baresip.h>
#include <twolame.h>
#include <string.h>
#include <speex/speex_resampler.h>
#include "mpa.h"

struct auenc_state {
	twolame_options *enc;
	int channels;
	SpeexResamplerState *resampler;
	int16_t intermediate_buffer[BARESIP_FRAMESIZE];
};


static void destructor(void *arg)
{
	struct auenc_state *aes = arg;

	if (aes->enc)
		twolame_close(&aes->enc);
#ifdef DEBUG
	debug("mpa: encoder destroyed\n");
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
	if (aes) {
		info("ever?");
		mem_deref(aes);
	}

	aes = mem_zalloc(sizeof(*aes), destructor);
	aes->enc = twolame_init();
	if (!aes->enc) {
		error("mpa: encoder create failed");
		mem_deref(aes);
		return ENOMEM;
	}
	aes->channels = ac->ch;
#ifdef DEBUG
	debug("mpa: encoder created %s\n",fmtp);
#endif

	prm.samplerate = 32000;
	prm.bitrate    = 128000;
	prm.layer      = 2;
	prm.mode       = SINGLE_CHANNEL;
	mpa_decode_fmtp(&prm, fmtp);

	result = 0;
#ifdef DEBUG
	result |= twolame_set_verbosity(aes->enc, 5);
#else
	result |= twolame_set_verbosity(aes->enc, 0);
#endif

	result |= twolame_set_mode(aes->enc,
		prm.mode == SINGLE_CHANNEL ? TWOLAME_MONO :
		prm.mode == DUAL_CHANNEL ? TWOLAME_DUAL_CHANNEL :
		prm.mode == JOINT_STEREO ? TWOLAME_JOINT_STEREO :
		prm.mode == STEREO ? TWOLAME_STEREO : TWOLAME_AUTO_MODE);
	result |= twolame_set_version(aes->enc,
		prm.samplerate < 32000 ? TWOLAME_MPEG2 : TWOLAME_MPEG1);
	result |= twolame_set_bitrate(aes->enc, prm.bitrate/1000);
	result |= twolame_set_in_samplerate(aes->enc, prm.samplerate);
	result |= twolame_set_out_samplerate(aes->enc, prm.samplerate);
	result |= twolame_set_num_channels(aes->enc, 2);
	if (result!=0) {
		error("mpa: encoder set failed\n");
		err=EINVAL;
		goto out;
	}

	result = twolame_init_params(aes->enc);
	if (result!=0) {
		error("mpa: encoder init params failed\n");
		err=EINVAL;
		goto out;
	}

	twolame_print_config(aes->enc);

	if (prm.samplerate != 48000) {
		aes->resampler = speex_resampler_init(2, 48000,
			prm.samplerate, 3, &result);
		if (result!=RESAMPLER_ERR_SUCCESS) {
			error("mpa: resampler init failed %d\n",result);
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
		    const int16_t *sampv, size_t sampc)
{
	int n;
	spx_uint32_t intermediate_len,in_len;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	if (aes->resampler)  {
		in_len = (uint32_t)sampc/2;
		intermediate_len = sizeof(aes->intermediate_buffer)
			/ sizeof(aes->intermediate_buffer[0]);
		n=speex_resampler_process_interleaved_int(aes->resampler,
			sampv, &in_len, aes->intermediate_buffer,
			&intermediate_len);
		if (n!=RESAMPLER_ERR_SUCCESS || in_len != sampc/2) {
			warning("mpa: downsample error: %s %d %d\n",
				strerror(n), in_len, sampc/2);
			return EPROTO;
		}
		n = twolame_encode_buffer_interleaved(aes->enc,
			aes->intermediate_buffer, intermediate_len,
			buf+4, (int)(*len)-4);
#ifdef DEBUG
		debug("mpa encode %d %d %d %d %d\n",intermediate_len,sampc,
			aes->channels,*len,n);
#endif
	}
	else
		n = twolame_encode_buffer_interleaved(aes->enc, sampv,
				      (int)(sampc/2), buf+4, (int)(*len)-4);

	if (n < 0) {
		error("mpa: encode error: %s\n", strerror((int)n));
		return EPROTO;
	}

	if (n > 0) {
		*(uint32_t*)(void *)buf = 0;
		*len = n+4;
	}
	else
		*len = 0;

#ifdef DEBUG
	debug("mpa encode %d %d %d %d\n",sampc,aes->channels,*len,n);
#endif
	return 0;
}

