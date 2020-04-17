/**
 * @file aac/decode.c MPEG-4 AAC Decoder
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <fdk-aac/aacdecoder_lib.h>
#include "aac.h"


struct au_hdr {
	uint16_t offset;
	uint16_t size;
	uint16_t count;
};


struct audec_state {
	HANDLE_AACDECODER dec;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;

	if (ads->dec)
		aacDecoder_Close(ads->dec);
}


static int hdr_decode(struct au_hdr *au_data, const uint8_t *p,
                      const size_t plen)
{
	uint16_t au_headers_length;
	uint16_t au_data_offset;
	uint16_t au_data_length;
	uint16_t bits;

	if (plen < sizeof(uint16_t) * 2)
		return EPROTO;

	au_headers_length = ntohs(*(uint16_t *)(void *)&p[0]);
	au_data_offset = sizeof(uint16_t) + (au_headers_length / 8);
	au_data_length = plen - au_data_offset;
	au_data->count = (au_headers_length / (sizeof(uint16_t) * 8));

	au_data->offset = au_data_offset;

	bits = ntohs(*(uint16_t *)(void *)&p[2]);

	au_data->size = bits >> ((sizeof(uint16_t) * 8) - AAC_SIZELENGTH);

	if (au_data->size == 0) {
		warning("aac: decode: invalid access unit size (zero)\n",
		        au_data->size);
		return EBADMSG;
	}

	if (au_data->size > au_data_length) {
		debug("aac: decode: fragmented access unit "
		      "(au-data-size: %zu > packet-data-size: %zu)\n",
		      au_data->size, au_data_length);
	}

	if (au_data->size != au_data_length) {
		debug("aac: decode: multiple access units per packet (%zu)\n",
		      au_data->count);
	}

	return 0;
}


int aac_decode_update(struct audec_state **adsp, const struct aucodec *ac,
                      const char *fmtp)
{
	struct audec_state *ads;
	AAC_DECODER_ERROR error;
	int err = 0;
	(void)fmtp;

	struct pl config;
	char config_str[64];
	uint8_t config_bin[32];

	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;

	ads->dec = aacDecoder_Open(TT_MP4_RAW, 1);
	if (!ads->dec) {
		warning("aac: error opening decoder\n");
		err = ENOMEM;
		goto out;
	}

	info("aac: decode update: fmtp='%s'\n", fmtp);

	err = re_regex(fmtp, str_len(fmtp), "config=[0-9a-f]+", &config);
	if (err)
		goto out;

	err = pl_strcpy(&config, config_str, sizeof(config_str));
	if (err)
		goto out;

	err = str_hex(config_bin, strlen(config_str)/2, config_str);
	if (err)
		goto out;

	UCHAR *conf = config_bin;
	const UINT length = (UINT)strlen(config_str)/2;

	error = aacDecoder_ConfigRaw(ads->dec, &conf, &length);
	if (error != AAC_DEC_OK) {
		warning("aac: decode: set config error (0x%x)\n", error);
		err = EPROTO;
		goto out;
	}

	error  = aacDecoder_SetParam(ads->dec, AAC_PCM_MIN_OUTPUT_CHANNELS,
	                             aac_channels);
	error |= aacDecoder_SetParam(ads->dec, AAC_PCM_MAX_OUTPUT_CHANNELS,
	                             aac_channels);
	if (error != AAC_DEC_OK) {
		warning("aac: decode: set param error (0x%x)\n", error);
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


int aac_decode_frm(struct audec_state *ads, int fmt, void *sampv,
                   size_t *sampc, bool marker, const uint8_t *buf, size_t len)
{
	UCHAR *pBuffer = (UCHAR *)buf;
	UINT bufferSize = 0;
	UINT valid = 0;
	AAC_DECODER_ERROR error;
	INT size;
	int err = 0;
	size_t nsamp = 0;
	int16_t *s16 = sampv;
	UINT pos = 0;
	struct au_hdr au_data;
	(void)marker;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	err = hdr_decode(&au_data, buf, len);
	if (err)
		return err;

	pos = au_data.offset;
	pBuffer += pos;

	while (len > pos) {
		CStreamInfo *info;

		bufferSize = (UINT)len - pos;
		valid = bufferSize;

		error =
		    aacDecoder_Fill(ads->dec, &pBuffer, &bufferSize, &valid);
		if (error != AAC_DEC_OK) {
			warning("aac: aacDecoder_Fill() failed (0x%x)\n",
			        error);
			return EPROTO;
		}

		size = (INT)*sampc;

		error =
		    aacDecoder_DecodeFrame(ads->dec, &s16[nsamp], size, 0);
		if (error == AAC_DEC_NOT_ENOUGH_BITS) {
			warning("aac: aacDecoder_DecodeFrame() failed: "
			        "NOT ENOUGH BITS %u / %u\n",
			        bufferSize, valid);
			break;
		}
		if (error != AAC_DEC_OK) {
			warning(
			    "aac: aacDecoder_DecodeFrame() failed (0x%x)\n",
			    error);
			return EPROTO;
		}

		info = aacDecoder_GetStreamInfo(ads->dec);
		if (!info) {
			warning("aac: decode: unable to get stream info\n");
			return EBADMSG;
		}

		if (info->sampleRate != (INT)aac_samplerate) {
			warning(
			    "aac: decode: samplerate mismatch (%d != %d)\n",
			    info->sampleRate, aac_samplerate);
			return EPROTO;
		}
		if (info->numChannels != (INT)aac_channels) {
			warning(
			    "aac: decode: channels mismatch (%d != %d)\n",
			    info->numChannels, aac_channels);
			return EPROTO;
		}

		nsamp += (info->frameSize * info->numChannels);
		size -= (info->frameSize * info->numChannels);

		pos += bufferSize - valid;
		pBuffer += bufferSize - valid;
	}

	if (nsamp > *sampc)
		return ENOMEM;

	*sampc = nsamp;

	return 0;
}
