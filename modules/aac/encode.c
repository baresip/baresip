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


enum {
	FRAME_SIZE = 480
};


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
	(void)fmtp;

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
	error |= aacEncoder_SetParam(aes->enc, AACENC_GRANULE_LENGTH,
				     FRAME_SIZE);
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


int aac_encode_frm(struct auenc_state *aes,
		   bool *marker, uint8_t *buf, size_t *len,
		   int fmt, const void *sampv, size_t sampc)
{
	AACENC_BufDesc in_buf, out_buf;
	AACENC_InArgs  in_args;
	AACENC_OutArgs out_args;
	AACENC_ERROR error;
	INT in_id = IN_AUDIO_DATA, in_size, in_elem_size = sizeof(int16_t);
	INT out_id = OUT_BITSTREAM_DATA, out_size, out_elem_size = 1;
	const int16_t *s16 = sampv;
	size_t total = 0;
	size_t i;
	(void)marker;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	for (i=0; i<sampc; i+=FRAME_SIZE) {

		in_size = sizeof(int16_t) * FRAME_SIZE;

		in_buf.numBufs           = 1;
		in_buf.bufs              = (void **)&s16;
		in_buf.bufferIdentifiers = &in_id;
		in_buf.bufSizes          = &in_size;
		in_buf.bufElSizes        = &in_elem_size;

		out_size = (INT)*len;

		out_buf.numBufs           = 1;
		out_buf.bufs              = (void **)&buf;
		out_buf.bufferIdentifiers = &out_id;
		out_buf.bufSizes          = &out_size;
		out_buf.bufElSizes        = &out_elem_size;

		in_args.numInSamples = FRAME_SIZE;
		in_args.numAncBytes = 0;

		error = aacEncEncode(aes->enc, &in_buf, &out_buf, &in_args,
				     &out_args);
		if (error != AACENC_OK) {
			warning("aac: Unable to encode frame: 0x%x\n", error);
			return EINVAL;
		}

		s16 += FRAME_SIZE;

		buf += out_args.numOutBytes;
		total += out_args.numOutBytes;
	}

	*len = total;

	return 0;
}
