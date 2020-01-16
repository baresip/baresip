/**
 * @file mpa/encode.c mpa Encode
 *
 * Copyright (C) 2016 Symonics GmbH
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <twolame.h>
#include <lame/lame.h>
#include <string.h>
#include <speex/speex_resampler.h>
#include "mpa.h"


struct auenc_state {
	twolame_options *enc2;
	lame_global_flags *enc3;
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

	if (aes->enc2)
		twolame_close(&aes->enc2);
	if (aes->enc3)
		lame_close(aes->enc3);
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

	debug("mpa: encoder fmtp (%s)\n", fmtp);

	/* Save the incoming MPA parameters from SDP offer */
	if (str_isset(fmtp)) {
		mpa_mirror_params(fmtp);
	}

	aes = *aesp;
	if (!aes) {
		aes = mem_zalloc(sizeof(*aes), destructor);
		if (!aes)
			return ENOMEM;

	}
	else
		memset(aes,0,sizeof(*aes));

	prm.samplerate = 48000;
	prm.bitrate    = 64000;
	prm.layer      = 2;
	prm.mode       = MONO;
	mpa_decode_fmtp(&prm, fmtp);

	if (prm.layer == 2)
		aes->enc2 = twolame_init();
	if (prm.layer == 3)
		aes->enc3 = lame_init();
	if (!aes->enc2 && !aes->enc3) {
		warning("MPA enc create failed\n");
		mem_deref(aes);
		return ENOMEM;
	}
#ifdef DEBUG
	debug("MPA enc created %s\n", fmtp);
#endif
	aes->channels = ac->ch;
	aes->samplerate = prm.samplerate;

	result = 0;

	if (aes->enc2) {
#ifdef DEBUG
		result |= twolame_set_verbosity(aes->enc2, 5);
#else
		result |= twolame_set_verbosity(aes->enc2, 0);
#endif
		result |= twolame_set_mode(aes->enc2, prm.mode);
		result |= twolame_set_version(aes->enc2,
					      prm.samplerate < 32000 ?
					      TWOLAME_MPEG2 : TWOLAME_MPEG1);
		result |= twolame_set_bitrate(aes->enc2, prm.bitrate/1000);
		result |= twolame_set_in_samplerate(aes->enc2,
						    prm.samplerate);
		result |= twolame_set_out_samplerate(aes->enc2,
						     prm.samplerate);
		result |= twolame_set_num_channels(aes->enc2, 2);
	}
	if (aes->enc3) {
		result |= lame_set_mode(aes->enc3, prm.mode);
		result |= lame_set_brate(aes->enc3, prm.bitrate/1000);
		result |= lame_set_in_samplerate(aes->enc3,
						 prm.samplerate);
		result |= lame_set_out_samplerate(aes->enc3,
						  prm.samplerate);
		result |= lame_set_num_channels(aes->enc3, 2);
		result |= lame_set_VBR(aes->enc3, vbr_off);
		result |= lame_set_bWriteVbrTag(aes->enc3, 0);
		result |= lame_set_strict_ISO(aes->enc3, 1);
		result |= lame_set_disable_reservoir(aes->enc3, 1);
	}
	if (result!=0) {
		warning("MPA enc set failed\n");
		err=EINVAL;
		goto out;
	}

	if (aes->enc2)
		result = twolame_init_params(aes->enc2);
	if (aes->enc3)
		result = lame_init_params(aes->enc3);
	if (result!=0) {
		warning("MPA enc init params failed\n");
		err=EINVAL;
		goto out;
	}
#ifdef DEBUG
	if (aes->enc2)
		twolame_print_config(aes->enc2);
	if (aes->enc3)
		lame_print_config(aes->enc3);
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


int mpa_encode_frm(struct auenc_state *aes,
		   bool *marker, uint8_t *buf, size_t *len,
		   int fmt, const void *sampv, size_t sampc)
{
	int n = 0;
	spx_uint32_t intermediate_len,in_len;
	uint32_t ts_delta = 0;
	(void)marker;

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
		if (aes->enc2) {
			n = twolame_encode_buffer_interleaved(aes->enc2,
				aes->intermediate_buffer, intermediate_len,
				buf+4, (int)(*len)-4);
#ifdef DEBUG
			debug("MPA enc %d %d %d %d %d %p\n",intermediate_len,
				sampc,aes->channels,*len,n,aes->enc2);
#endif
		}
		if (aes->enc3) {
			n = lame_encode_buffer_interleaved(aes->enc3,
				aes->intermediate_buffer, intermediate_len,
				buf+4, (int)(*len)-4);
#ifdef DEBUG
			debug("MPA enc %d %d %d %d %d %p\n",intermediate_len,
				sampc,aes->channels,*len,n,aes->enc3);
#endif
		}
	}
	else {
		if (aes->enc2)
			n = twolame_encode_buffer_interleaved(aes->enc2,
				sampv, (int)(sampc/2),
				buf+4, (int)(*len)-4);
		if (aes->enc3)
			n = lame_encode_buffer_interleaved(aes->enc3,
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
	if (aes->enc2)
		debug("MPA enc done %d %d %d %d %p\n",sampc,aes->channels,
			*len,n,aes->enc2);
	if (aes->enc3)
		debug("MPA enc done %d %d %d %d %p\n",sampc,aes->channels,
			*len,n,aes->enc3);
#endif

	return 0x00010000 | ((ts_delta>>4) & 0x0000ffff);
}

