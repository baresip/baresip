/**
 * @file g729.c G.729 Audio Codec
 *
 * Copyright (C) 2020 Juha Heinanen
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <bcg729/decoder.h>
#include <bcg729/encoder.h>


/**
 * @defgroup g729 g729
 *
 * The G.729 audio codec
 */


struct auenc_state {
	bcg729EncoderChannelContextStruct* enc;
};

struct audec_state {
	bcg729DecoderChannelContextStruct* dec;
};


static void encode_destructor(void *data)
{
	struct auenc_state *st = data;

	if (st->enc)
		closeBcg729EncoderChannel(st->enc);
}


static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	int err = 0;
	(void)prm;
	(void)fmtp;

	if (!aesp || !ac)
		return EINVAL;

	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->enc = initBcg729EncoderChannel(0 /* no VAT/DTX detection */);
	if (!st->enc) {
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*aesp = st;

	return err;
}


static void decode_destructor(void *data)
{
	struct audec_state *st = data;

	if (st->dec)
		closeBcg729DecoderChannel(st->dec);
}


static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct audec_state *st;
	int err = 0;
	(void)fmtp;

	if (!adsp || !ac)
		return EINVAL;

	if (*adsp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->dec = initBcg729DecoderChannel();
	if (!st->dec) {
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


static int encode(struct auenc_state *aes, bool *marker, uint8_t *buf,
		  size_t *len, int fmt, const void *sampv, size_t sampc)
{
	unsigned int i, count;
	uint8_t olen;
	(void)marker;

	if (!buf || !len || !sampv)
		return EINVAL;

	if ((sampc % 160) != 0)
		return EPROTO;

	count = sampc / 80;

	for (i = 0; i < count; i++)
		bcg729Encoder(aes->enc, (int16_t*)sampv + i * 80,
			      (uint8_t*)buf + i * 10, &olen);

	*len = count * 10;

	return 0;
}


static int decode(struct audec_state *ads, int fmt, void *sampv,
		  size_t *sampc, bool marker, const uint8_t *buf, size_t len)
{
	(void)marker;
	unsigned int i;

	if (!sampv || !sampc || !buf)
		return EINVAL;

	for (i = 0; i < (len / 10); i++) {
		bcg729Decoder(ads->dec, buf + i * 10, 10, 0, 0, 0,
			      (int16_t *)sampv + i * 80);
	}

	*sampc = 80 * (len / 10);

	return 0;
}


static struct aucodec g729 = {
	.pt    = "18",
	.name  = "G729",
	.srate = 8000,
	.crate = 8000,
	.ch    = 1,
	.pch   = 1,
	.encupdh   = encode_update,
	.ench      = encode,
	.decupdh   = decode_update,
	.dech      = decode,
};


static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), &g729);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&g729);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(g729) = {
	"g729",
	"audio codec",
	module_init,
	module_close,
};
