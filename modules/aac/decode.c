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

	unsigned sample_rate;
	unsigned frame_size;
	unsigned channels;
	bool set;
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


static int get_stream_info(struct audec_state *ads)
{
	CStreamInfo *info     = aacDecoder_GetStreamInfo(ads->dec);

	if (!info) {
		warning("Unable to get stream info\n");
		return EPROTO;
	}

	if (info->sampleRate <= 0) {
		warning("Stream info not initialized\n");
		return EPROTO;
	}

	ads->sample_rate = info->sampleRate;
	ads->channels = info->numChannels;
	ads->frame_size  = info->frameSize;

	if (!ads->set) {
		re_printf("aac stream info:\n");
		re_printf(".... samplerate: %u\n", ads->sample_rate);
		re_printf(".... frame_size: %u\n", ads->frame_size);
		re_printf(".... channels:   %u\n", ads->channels);
		ads->set = true;
	}

	return 0;
}


int aac_decode_frm(struct audec_state *ads,
		   int fmt, void *sampv, size_t *sampc,
		   const uint8_t *buf, size_t len)
{
	UCHAR *pBuffer = (UCHAR *)buf;
	UINT bufferSize = (UINT)len;
	UINT valid = (UINT)len;
	AAC_DECODER_ERROR error;
	int ret;
	size_t nsamp = 0;
	unsigned i;
	int16_t *s16 = sampv;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	error = aacDecoder_Fill(ads->dec, &pBuffer, &bufferSize, &valid);
	if (error != AAC_DEC_OK) {
		warning("aac: aacDecoder_Fill() failed: 0x%x\n", error);
		return EPROTO;
	}

	int size = (int)*sampc;

	for (i=0; i<8; i++) {

		error = aacDecoder_DecodeFrame(ads->dec,
					       &s16[nsamp],
					       size,
					       0);
		if (error == AAC_DEC_NOT_ENOUGH_BITS) {
			break;
		}
		if (error != AAC_DEC_OK) {
			warning("aac: aacDecoder_DecodeFrame() failed: 0x%x\n",
				error);
			return EPROTO;
		}

		ret = get_stream_info(ads);
		if (ret) {
			warning("aac: could not get stream info\n");
			return ret;
		}

		if (ads->sample_rate != AAC_SRATE) {
			warning("aac: sample rate mismatch\n");
			return EPROTO;
		}
		if (ads->channels != AAC_CHANNELS) {
			warning("aac: channels mismatch\n");
			return EPROTO;
		}

		nsamp += (ads->frame_size * ads->channels);
		size  -= (ads->frame_size * ads->channels);
	}

	if (nsamp > *sampc)
		return ENOMEM;

	*sampc = nsamp;

	return 0;
}
