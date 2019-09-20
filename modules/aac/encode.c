/**
 * @file aac/encode.c AAC Encoder
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <fdk-aac/aacenc_lib.h>
#include "aac.h"


struct auenc_state {
	HANDLE_AACENCODER enc;
};


static void destructor(void *arg)
{
	struct auenc_state *aes = arg;

	if (aes->enc)
		aacEncClose(&aes->enc);
}


int aac_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
		       struct auenc_param *param, const char *fmtp)
{
	struct auenc_state *aes;
	AACENC_ERROR error;
	int err = 0;

	(void)param;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	aes = *aesp;

	if (aes)
		goto out;

	aes = mem_zalloc(sizeof(*aes), destructor);
	if (!aes)
		return ENOMEM;

	error = aacEncOpen(&aes->enc, 0, AAC_CHANNELS);
	if (error != AACENC_OK) {
		warning("aac: Unable to open the encoder: 0x%x\n",
			error);
		err = ENOMEM;
		goto out;
	}

	error |= aacEncoder_SetParam(aes->enc, AACENC_AOT, AOT_ER_AAC_LD);
	error |= aacEncoder_SetParam(aes->enc, AACENC_TRANSMUX,
				     TT_MP4_LATM_MCP1);
	error |= aacEncoder_SetParam(aes->enc, AACENC_SAMPLERATE, ac->srate);
	error |= aacEncoder_SetParam(aes->enc, AACENC_CHANNELMODE,
				     AAC_CHANNELS);
	error |= aacEncoder_SetParam(aes->enc, AACENC_GRANULE_LENGTH, 480);
	error |= aacEncoder_SetParam(aes->enc, AACENC_TPSUBFRAMES, 2);
	error |= aacEncoder_SetParam(aes->enc, AACENC_BITRATE, AAC_BITRATE);
	if (error != AACENC_OK) {
		err = EINVAL;
		goto out;
	}

	*aesp = aes;

 out:
	if (err)
		mem_deref(aes);

	return err;
}


int aac_encode_frm(struct auenc_state *aes, uint8_t *buf, size_t *len,
		    int fmt, const void *sampv, size_t sampc)
{
	AACENC_BufDesc in_buf   = { 0 }, out_buf = { 0 };
	AACENC_InArgs  in_args  = { 0 };
	AACENC_OutArgs out_args = { 0 };
	int in_buffer_identifier = IN_AUDIO_DATA;
	int in_buffer_size, in_buffer_element_size;
	int out_buffer_identifier = OUT_BITSTREAM_DATA;
	int out_buffer_size, out_buffer_element_size;
	void *in_ptr, *out_ptr;
	AACENC_ERROR error;
	const int16_t *s16 = sampv;
	int i;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	const unsigned framesize = 480;
	int total = 0;

	for (i=0; i<2; i++) {

		in_ptr               = (void *)s16;
		in_buffer_size       = sizeof(int16_t) * (int)framesize;

		in_args.numInSamples = (int)framesize;

		in_buffer_element_size   = 2;
		in_buf.numBufs           = 1;
		in_buf.bufs              = &in_ptr;
		in_buf.bufferIdentifiers = &in_buffer_identifier;
		in_buf.bufSizes          = &in_buffer_size;
		in_buf.bufElSizes        = &in_buffer_element_size;

		out_ptr                   = buf;
		out_buffer_size           = (int)*len;
		out_buffer_element_size   = 1;
		out_buf.numBufs           = 1;
		out_buf.bufs              = &out_ptr;
		out_buf.bufferIdentifiers = &out_buffer_identifier;
		out_buf.bufSizes          = &out_buffer_size;
		out_buf.bufElSizes        = &out_buffer_element_size;

		error = aacEncEncode(aes->enc, &in_buf, &out_buf, &in_args,
				     &out_args);
		if (error != AACENC_OK) {
			warning("aac: Unable to encode frame: 0x%x\n", error);
			return EINVAL;
		}

		s16 += framesize;

		buf += out_args.numOutBytes;
		total += out_args.numOutBytes;
	}

	*len = total;

	return 0;
}
