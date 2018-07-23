/**
 * @file silk.c  Skype SILK audio codec
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <silk/SKP_Silk_SDK_API.h>


/**
 * @defgroup silk silk
 *
 * The Skype SILK audio codec
 *
 * References:  https://developer.skype.com/silk
 */


enum {
	MAX_BYTES_PER_FRAME = 250,
	MAX_FRAME_SIZE      = 2*480,
};


struct auenc_state {
	void *enc;
	SKP_SILK_SDK_EncControlStruct encControl;
};

struct audec_state {
	void *dec;
	SKP_SILK_SDK_DecControlStruct decControl;
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	mem_deref(st->enc);
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	mem_deref(st->dec);
}


static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	int ret, err = 0;
	int32_t enc_size;
	(void)fmtp;

	if (!aesp || !ac || !prm)
		return EINVAL;
	if (*aesp)
		return 0;

	ret = SKP_Silk_SDK_Get_Encoder_Size(&enc_size);
	if (ret || enc_size <= 0)
		return EINVAL;

	st = mem_alloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->enc = mem_alloc(enc_size, NULL);
	if (!st->enc) {
		err = ENOMEM;
		goto out;
	}

	ret = SKP_Silk_SDK_InitEncoder(st->enc, &st->encControl);
	if (ret) {
		err = EPROTO;
		goto out;
	}

	st->encControl.API_sampleRate = ac->srate;
	st->encControl.maxInternalSampleRate = ac->srate;
	st->encControl.packetSize = prm->ptime * ac->srate / 1000;
	st->encControl.bitRate = 64000;
	st->encControl.complexity = 2;
	st->encControl.useInBandFEC = 0;
	st->encControl.useDTX = 0;

	info("silk: encoder: %dHz, psize=%d, bitrate=%d, complex=%d,"
	     " fec=%d, dtx=%d\n",
	     st->encControl.API_sampleRate,
	     st->encControl.packetSize,
	     st->encControl.bitRate,
	     st->encControl.complexity,
	     st->encControl.useInBandFEC,
	     st->encControl.useDTX);

 out:
	if (err)
		mem_deref(st);
	else
		*aesp = st;

	return err;
}


static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct audec_state *st;
	int ret, err = 0;
	int32_t dec_size;
	(void)fmtp;

	if (*adsp)
		return 0;

	ret = SKP_Silk_SDK_Get_Decoder_Size(&dec_size);
	if (ret || dec_size <= 0)
		return EINVAL;

	st = mem_alloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->dec = mem_alloc(dec_size, NULL);
	if (!st->dec) {
		err = ENOMEM;
		goto out;
	}

	ret = SKP_Silk_SDK_InitDecoder(st->dec);
	if (ret) {
		err = EPROTO;
		goto out;
	}

	st->decControl.API_sampleRate = ac->srate;

 out:
	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


static int encode(struct auenc_state *st, uint8_t *buf, size_t *len,
		  int fmt, const void *sampv, size_t sampc)
{
	int ret;
	int16_t nBytesOut;

	if (*len < MAX_BYTES_PER_FRAME)
		return ENOMEM;
	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	nBytesOut = *len;
	ret = SKP_Silk_SDK_Encode(st->enc,
				  &st->encControl,
				  sampv,
				  (int)sampc,
				  buf,
				  &nBytesOut);
	if (ret) {
		warning("silk: SKP_Silk_SDK_Encode: ret=%d\n", ret);
	}

	*len = nBytesOut;

	return 0;
}


static int decode(struct audec_state *st, int fmt, void *sampv,
		  size_t *sampc, const uint8_t *buf, size_t len)
{
	int16_t nsamp = *sampc;
	int ret;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	ret = SKP_Silk_SDK_Decode(st->dec,
				  &st->decControl,
				  0,
				  buf,
				  (int)len,
				  sampv,
				  &nsamp);
	if (ret) {
		warning("silk: SKP_Silk_SDK_Decode: ret=%d\n", ret);
	}

	*sampc = nsamp;

	return 0;
}


static int plc(struct audec_state *st, int fmt, void *sampv, size_t *sampc)
{
	int16_t nsamp = *sampc;
	int ret;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	ret = SKP_Silk_SDK_Decode(st->dec,
				  &st->decControl,
				  1,
				  NULL,
				  0,
				  sampv,
				  &nsamp);
	if (ret)
		return EPROTO;

	*sampc = nsamp;

	return 0;
}


static struct aucodec silk[] = {
	{
		LE_INIT, 0, "SILK", 24000, 24000, 1, 1, NULL,
		encode_update, encode, decode_update, decode, plc, 0, 0
	},

};


static int module_init(void)
{
	debug("silk: SILK %s\n", SKP_Silk_SDK_get_version());

	aucodec_register(baresip_aucodecl(), &silk[0]);

	return 0;
}


static int module_close(void)
{
	int i = ARRAY_SIZE(silk);

	while (i--)
		aucodec_unregister(&silk[i]);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(silk) = {
	"silk",
	"codec",
	module_init,
	module_close
};
