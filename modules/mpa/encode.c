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

#undef DEBUG 

struct auenc_state {	
	twolame_options *enc;
	int channels;
	SpeexResamplerState *resampler;
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
	const struct aucodec *auc = aucodec_find("MPA", 90000, 1);
	int mpares;

	(void)param;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	aes = *aesp;
	
	if (aes) {
		mem_deref(aes);
	}
		
	aes = mem_zalloc(sizeof(*aes), destructor);
	aes->enc = twolame_init();
	if (!aes->enc) {
		error("mpa: encoder create failed");
		mem_deref(aes);
		return ENOMEM;
	}
	aes->channels = auc->ch;
	*aesp = aes;
#ifdef DEBUG
	debug("mpa: encoder created %s\n",fmtp);
#endif

	prm.samplerate = 32000;
	prm.bitrate    = 128000;
	prm.layer      = 2;
	prm.mode       = STEREO;
	mpa_decode_fmtp(&prm, fmtp);

	mpares = 0;
#ifdef DEBUG
	mpares |= twolame_set_verbosity(aes->enc, 5);	
#else
	mpares |= twolame_set_verbosity(aes->enc, 0);	
#endif

	mpares |= twolame_set_mode(aes->enc, prm.mode == SINGLE_CHANNEL ? TWOLAME_MONO : 
						prm.mode == DUAL_CHANNEL ? TWOLAME_DUAL_CHANNEL : 
						prm.mode == JOINT_STEREO ? TWOLAME_JOINT_STEREO :
						prm.mode == STEREO ? TWOLAME_STEREO : TWOLAME_AUTO_MODE);
	mpares |= twolame_set_version(aes->enc, prm.samplerate < 32000 ? TWOLAME_MPEG2 : TWOLAME_MPEG1);
	mpares |= twolame_set_bitrate(aes->enc, prm.bitrate/1000);
	mpares |= twolame_set_in_samplerate(aes->enc, prm.samplerate);
	mpares |= twolame_set_out_samplerate(aes->enc, prm.samplerate);
	mpares |= twolame_set_num_channels(aes->enc, 2);
	if(mpares!=0) {
		error("mpa: encoder set failed\n");
		return EINVAL;
	}

	mpares = twolame_init_params(aes->enc);
	if(mpares!=0) {
		error("mpa: encoder init params failed\n");
		return EINVAL;
	}

	twolame_print_config(aes->enc);


	if(prm.samplerate != 48000) {
		aes->resampler = speex_resampler_init(2, 48000, prm.samplerate, 3, &mpares);
		if(mpares!=RESAMPLER_ERR_SUCCESS) {
			error("mpa: resampler init failed %d\n",mpares);
			return EINVAL;
		}

	}
	else
		aes->resampler = NULL;
	return 0;
}


int mpa_encode_frm(struct auenc_state *aes, uint8_t *buf, size_t *len,
		    const int16_t *sampv, size_t sampc)
{
	int n;
	spx_uint32_t ds_len,in_len;
	static int16_t ds[1920];

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	if(aes->resampler)  {
		in_len = sampc/2;
		ds_len = 1920;
		n=speex_resampler_process_interleaved_int(aes->resampler, sampv, &in_len, ds, &ds_len);
		if (n!=RESAMPLER_ERR_SUCCESS || in_len != sampc/2) {
			warning("mpa: downsample error: %s %d %d\n", strerror(n), in_len, sampc/2);
			return EPROTO;
		}
		n = twolame_encode_buffer_interleaved(aes->enc, ds, ds_len,
			buf+4, (*len)-4);
#ifdef DEBUG
		debug("mpa encode %d %d %d %d %d\n",ds_len,sampc,aes->channels,*len,n);
#endif
	}
	else
		n = twolame_encode_buffer_interleaved(aes->enc, sampv, (int)(sampc/2),
			buf+4, (*len)-4);

	if (n < 0) {
		error("mpa: encode error: %s\n", strerror((int)n));
		return EPROTO;
	}

	if(n > 0) {
		*(uint32_t*)buf = 0;
		*len = n+4;
	}
	else
		*len = 0;

#ifdef DEBUG
	debug("mpa encode %d %d %d %d\n",sampc,aes->channels,*len,n);
#endif
	return 0;
}

