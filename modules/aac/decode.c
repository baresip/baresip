/**
 * @file aac/decode.c AAC Decoder
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <fdk-aac/aacdecoder_lib.h>
#include "aac.h"


struct audec_state {
	HANDLE_AACDECODER dec;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;

	if (ads->dec)
		aacDecoder_Close(ads->dec);
}


int aac_decode_update(struct audec_state **adsp, const struct aucodec *ac,
		       const char *fmtp)
{
	struct audec_state *ads;
	int err = 0;
	(void)fmtp;

	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;

	ads->dec = aacDecoder_Open(TT_MP4_LATM_MCP1, 1);
	if (!ads->dec) {
		warning("aac: error opening decoder\n");
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


int aac_decode_frm(struct audec_state *ads,
		   int fmt, void *sampv, size_t *sampc,
		   bool marker, const uint8_t *buf, size_t len)
{
	UCHAR *pBuffer = (UCHAR *)buf;
	UINT bufferSize = (UINT)len;
	UINT valid = (UINT)len;
	AAC_DECODER_ERROR error;
	size_t nsamp = 0;
	unsigned i;
	int16_t *s16 = sampv;
	int size;
	(void)marker;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	error = aacDecoder_Fill(ads->dec, &pBuffer, &bufferSize, &valid);
	if (error != AAC_DEC_OK) {
		warning("aac: aacDecoder_Fill() failed: 0x%x\n", error);
		return EPROTO;
	}

	size = (int)*sampc;

	for (i=0; i<8; i++) {

		CStreamInfo *info;

		error = aacDecoder_DecodeFrame(ads->dec, &s16[nsamp], size, 0);
		if (error == AAC_DEC_NOT_ENOUGH_BITS)
			break;

		if (error != AAC_DEC_OK) {
			warning("aac: aacDecoder_DecodeFrame() failed: 0x%x\n",
				error);
			return EPROTO;
		}

		info = aacDecoder_GetStreamInfo(ads->dec);
		if (!info) {
			warning("aac: Unable to get stream info\n");
			return EBADMSG;
		}

		if (info->sampleRate != AAC_SRATE) {
			warning("aac: decode samplerate mismatch (%d != %d)\n",
				info->sampleRate, AAC_SRATE);
			return EPROTO;
		}
		if (info->numChannels != AAC_CHANNELS) {
			warning("aac: decode channels mismatch (%d != %d)\n",
				info->numChannels, AAC_CHANNELS);
			return EPROTO;
		}

		nsamp += (info->frameSize * info->numChannels);
		size  -= (info->frameSize * info->numChannels);
	}

	if (nsamp > *sampc)
		return ENOMEM;

	*sampc = nsamp;

	return 0;
}
