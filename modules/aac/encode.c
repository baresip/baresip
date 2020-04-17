/**
 * @file aac/encode.c MPEG-4 AAC Encoder
 *
 * Copyright (C) 2010 Creytiv.com
 * Copyright (C) 2019 Hessischer Rundfunk
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


static void hdr_encode(uint8_t *p, uint16_t size)
{
	uint8_t n = 1; /* we only have a single AU-header!!! */
	/* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
	 * |AU-headers-length|AU-header|AU-header|      |AU-header|padding|
	 * |                 |   (1)   |   (2)   |      |   (n)   | bits  |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
	 */

	/* AU-headers-length */
	*(uint16_t *)(void *)&p[0] = htons(sizeof(uint16_t) * 8 * n);

	/* +---------------------------------------+
	 * |     AU-size                           |
	 * +---------------------------------------+
	 * |     AU-Index / AU-Index-delta         |
	 * +---------------------------------------+
	 * |     CTS-flag                          |
	 * +---------------------------------------+
	 * |     CTS-delta                         |
	 * +---------------------------------------+
	 * |     DTS-flag                          |
	 * +---------------------------------------+
	 * |     DTS-delta                         |
	 * +---------------------------------------+
	 * |     RAP-flag                          |
	 * +---------------------------------------+
	 * |     Stream-state                      |
	 * +---------------------------------------+
	 */

	/* The AU-header, no CTS, DTS, RAP, Stream-state
	 *
	 * AU-size is always the total size of the AU, not the fragmented
	 * size
	 */
	*(uint16_t *)(void *)&p[2] =
	    htons(size << ((sizeof(uint16_t) * 8) - AAC_SIZELENGTH));
}


int aac_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
                      struct auenc_param *param, const char *fmtp)
{
	struct auenc_state *aes;
	struct aac_param prm;
	uint32_t enc_aot, enc_ratio;
	AACENC_InfoStruct enc_info;
	AACENC_ERROR error;
	int err = 0;
	(void)param;
	(void)fmtp;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	debug("aac: encoder fmtp (%s)\n", fmtp);

	if (str_isset(fmtp)) {
		aac_mirror_params(fmtp);
		aac_decode_fmtp(&prm, fmtp);
	}
	else {
		prm.profile_level_id = aac_profile;
		prm.bitrate = aac_bitrate;
		prm.constantduration = aac_constantduration;
	}

	aes = *aesp;

	if (!aes) {
		aes = mem_zalloc(sizeof(*aes), destructor);
		if (!aes)
			return ENOMEM;

		error = aacEncOpen(&aes->enc, 0, 0);
		if (error != AACENC_OK) {
			warning("aac: Unable to open the encoder (0x%x)\n",
			        error);
			err = ENOMEM;
			goto out;
		}

		if ((prm.profile_level_id >= 14 &&
		    prm.profile_level_id <= 21) ||
		    (prm.profile_level_id >= 40 &&
		    prm.profile_level_id <= 43)) {
			info("aac: Encoder Profile AAC-LC\n");
			enc_aot = AOT_AAC_LC; /* AAC-LC */
			enc_ratio = 1;
		}
		else if (prm.profile_level_id == 52 ||
		         (prm.profile_level_id >= 22 &&
		          prm.profile_level_id <= 29)) {
			info("aac: Encoder Profile AAC-LD\n");
			enc_aot = AOT_ER_AAC_LD; /* AAC-LD */
			enc_ratio = 1;
		}
		else if (prm.profile_level_id >= 76 &&
		         prm.profile_level_id <= 77) {
			info("aac: Encoder Profile AAC-ELD\n");
			enc_aot = AOT_ER_AAC_ELD; /* AAC-ELD */
			enc_ratio = 1;
			switch (ac->ch) {
			case MODE_1: /* mono */
				prm.profile_level_id = 76;
				break;
			case MODE_2: /* stereo */
				prm.profile_level_id = 77;
				break;
			}
		}
		else if (prm.profile_level_id >= 44 &&
		         prm.profile_level_id <= 47) {
			info("aac: Encoder Profile HE-AAC\n");
			enc_aot = AOT_SBR; /* HE-AAC */
			enc_ratio = 2; /* SBR */
		}
		else if (prm.profile_level_id >= 48 &&
		         prm.profile_level_id <= 51 &&
			 ac->ch == MODE_2) {
			info("aac: Encoder Profile HE-AAC v2\n");
			enc_aot = AOT_PS; /* HE-AAC v2 */
			enc_ratio = 2; /* SBR */
		}
		else {
			err = EINVAL;
			goto out;
		}

		debug("srate: %u, crate: %u, ch: %u, pch: %u, ptime: %u\n",
		      ac->srate, ac->crate, ac->ch, ac->pch, ac->ptime);

		/* set mandatory encoder params: */
		error |= aacEncoder_SetParam(aes->enc, AACENC_AOT,
		                             enc_aot);
		error |= aacEncoder_SetParam(aes->enc, AACENC_SAMPLERATE,
		                             ac->srate);
		error |= aacEncoder_SetParam(aes->enc, AACENC_CHANNELMODE,
		                             ac->ch);
		error |= aacEncoder_SetParam(aes->enc, AACENC_BITRATE,
		                             prm.bitrate);
		error |= aacEncoder_SetParam(aes->enc, AACENC_TRANSMUX,
		                             TT_MP4_RAW);
		/* set object specific encoder params: */
		error |= aacEncoder_SetParam(aes->enc, AACENC_GRANULE_LENGTH,
		                             prm.constantduration/enc_ratio);
		/* set optional encoder params: */
		error |= aacEncoder_SetParam(aes->enc, AACENC_BITRATEMODE,
		                             0); /* AACENC_BR_MODE_CBR */
		error |= aacEncoder_SetParam(aes->enc, AACENC_AFTERBURNER,
		                             1);
		if (error != AACENC_OK) {
			err = EINVAL;
			goto out;
		}

		error = aacEncEncode(aes->enc, NULL, NULL, NULL, NULL);
		if (error != AACENC_OK) {
			warning(
			    "aac: Unable to initialize the encoder (0x%x)\n",
			    error);
			err = EINVAL;
			goto out;
		}

		error = aacEncInfo(aes->enc, &enc_info);
		if (error != AACENC_OK) {
			warning(
			    "aac: Failed to get AAC encoder info (0x%x)\n",
			    error);
			err = EINVAL;
			goto out;
		}

		re_snprintf(prm.config, sizeof(prm.config), "%w",
		            enc_info.confBuf, enc_info.confSize);

		prm.constantduration = enc_info.frameLength;
		prm.bitrate = aacEncoder_GetParam(aes->enc, AACENC_BITRATE);

		debug("aac: Encoder configuration: conf=%w, "
		     "frameLength=%u, inputChannels=%u\n",
		     enc_info.confBuf, enc_info.confSize,
		     enc_info.frameLength, enc_info.inputChannels
		     );

		debug("aac: encoder setup:\n"
		      "\tAOT=%u\n"
		      "\tBITRATE=%u\n"
		      "\tBITRATEMODE=%u\n"
		      "\tSAMPLERATE=%u\n"
		      "\tSBR_MODE=%u\n"
		      "\tGRANULE_LENGTH=%u\n"
		      "\tCHANNELMODE=%u\n"
		      "\tCHANNELORDER=%u\n"
		      "\tSBR_RATIO=%u\n"
		      "\tAFTERBURNER=%u\n"
		      "\tBANDWIDTH=%u\n"
		      "\tTRANSMUX=%u\n"
		      "\tHEADER PERIOD=%u\n"
		      "\tSIGNALING_MODE=%u\n"
		      "\tTPSUBFRAMES=%u\n"
		      "\tPROTECTION=%u\n"
		      "\tANCILLARY_BITRATE=%u\n"
		      "\tMETADATA_MODE=%u\n",
		      aacEncoder_GetParam(aes->enc, AACENC_AOT),
		      aacEncoder_GetParam(aes->enc, AACENC_BITRATE),
		      aacEncoder_GetParam(aes->enc, AACENC_BITRATEMODE),
		      aacEncoder_GetParam(aes->enc, AACENC_SAMPLERATE),
		      aacEncoder_GetParam(aes->enc, AACENC_SBR_MODE),
		      aacEncoder_GetParam(aes->enc, AACENC_GRANULE_LENGTH),
		      aacEncoder_GetParam(aes->enc, AACENC_CHANNELMODE),
		      aacEncoder_GetParam(aes->enc, AACENC_CHANNELORDER),
		      aacEncoder_GetParam(aes->enc, AACENC_SBR_RATIO),
		      aacEncoder_GetParam(aes->enc, AACENC_AFTERBURNER),
		      aacEncoder_GetParam(aes->enc, AACENC_BANDWIDTH),
		      aacEncoder_GetParam(aes->enc, AACENC_TRANSMUX),
		      aacEncoder_GetParam(aes->enc, AACENC_HEADER_PERIOD),
		      aacEncoder_GetParam(aes->enc, AACENC_SIGNALING_MODE),
		      aacEncoder_GetParam(aes->enc, AACENC_TPSUBFRAMES),
		      aacEncoder_GetParam(aes->enc, AACENC_PROTECTION),
		      aacEncoder_GetParam(aes->enc, AACENC_ANCILLARY_BITRATE),
		      aacEncoder_GetParam(aes->enc, AACENC_METADATA_MODE));

		/* aac_encode_fmtp(&prm); */
	}
	*aesp = aes;

out:
	if (err)
		mem_deref(aes);

	return err;
}


int aac_encode_frm(struct auenc_state *aes, bool *marker, uint8_t *buf,
                   size_t *len, int fmt, const void *sampv, size_t sampc)
{
	/*warning("aac_encode_frm()\n");*/
	AACENC_BufDesc in_buf, out_buf;
	AACENC_InArgs in_args;
	AACENC_OutArgs out_args;
	AACENC_ERROR error;
	INT in_id = IN_AUDIO_DATA, in_size, in_elem_size = sizeof(int16_t);
	INT out_id = OUT_BITSTREAM_DATA, out_size, out_elem_size = 1;

	const int16_t *s16 = sampv;
	INT total = 0;
	INT sampi = 0;

	/* uint16_t au_sizes[UINT8_MAX]; */
	uint8_t i = 0;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	/*
	 * ToDo: Implement encoding of multiple
	 * access units. May need intermediate
	 * buffering of au data before writing
	 * headers and header length.
	 */

	buf += AU_HDR_LEN; /* single access unit only! */

	while (((INT)sampc > sampi) && (i < UINT8_MAX)) {
		in_size = (INT)sizeof(int16_t) * ((INT)sampc - sampi);

		in_buf.numBufs = 1;
		in_buf.bufs = (void **)&s16;
		in_buf.bufferIdentifiers = &in_id;
		in_buf.bufSizes = &in_size;
		in_buf.bufElSizes = &in_elem_size;

		out_size = (INT)*len - total - AU_HDR_LEN; /* 1 au only! */

		out_buf.numBufs = 1;
		out_buf.bufs = (void **)&buf;
		out_buf.bufferIdentifiers = &out_id;
		out_buf.bufSizes = &out_size;
		out_buf.bufElSizes = &out_elem_size;

		in_args.numInSamples = (INT)sampc - sampi;
		in_args.numAncBytes = 0;

		error = aacEncEncode(aes->enc, &in_buf, &out_buf, &in_args,
		                     &out_args);
		if (error != AACENC_OK) {
			warning("aac: aacEncEncode() failed (0x%x)\n",
			        error);
			return EINVAL;
		}

		sampi += out_args.numInSamples;
		s16 += out_args.numInSamples;

		buf += out_args.numOutBytes;
		total += out_args.numOutBytes;

		if (out_args.numOutBytes > 0) {
			/* au_sizes[i] = out_args.numOutBytes; */
			if (i > 0)
			 	/* single access unit only! */
				warning("aac: Sorry, encoding multiple AU "
				        "per packet is not implemented yet.\n"
				        "Please reduce the amount of samples "
				        "passed to encoder per packet by "
				        "lowering ptime value.\n");
			++i;
		}
	}

	if (total == 0) {
		*len = 0;
		return 0;
 	}

	*marker = true;

	buf -= total + AU_HDR_LEN; /* single access unit only! */

	hdr_encode(buf, total);

	*len = total + AU_HDR_LEN; /* single access unit only! */

	return 0;
}
