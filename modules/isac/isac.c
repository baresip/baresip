/**
 * @file isac.c iSAC audio codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "isac.h"


/**
 * @defgroup isac isac
 *
 * iSAC audio codec
 *
 * draft-ietf-avt-rtp-isac-04
 */


struct auenc_state {
	ISACStruct *inst;
};

struct audec_state {
	ISACStruct *inst;
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	if (st->inst)
		WebRtcIsac_Free(st->inst);
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	if (st->inst)
		WebRtcIsac_Free(st->inst);
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

	st = mem_alloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	if (WebRtcIsac_Create(&st->inst) < 0) {
		err = ENOMEM;
		goto out;
	}

	WebRtcIsac_EncoderInit(st->inst, 0);

	if (ac->srate == 32000)
		WebRtcIsac_SetEncSampRate(st->inst, kIsacSuperWideband);

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
	int err = 0;
	(void)fmtp;

	if (!adsp || !ac)
		return EINVAL;

	if (*adsp)
		return 0;

	st = mem_alloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	if (WebRtcIsac_Create(&st->inst) < 0) {
		err = ENOMEM;
		goto out;
	}

	WebRtcIsac_DecoderInit(st->inst);

	if (ac->srate == 32000)
		WebRtcIsac_SetDecSampRate(st->inst, kIsacSuperWideband);

 out:
	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


static int encode(struct auenc_state *st, uint8_t *buf, size_t *len,
		  const int16_t *sampv, size_t sampc)
{
	WebRtc_Word16 len1, len2;
	size_t l;

	if (!st || !buf || !len || !sampv || !sampc)
		return EINVAL;

	/* 10 ms audio blocks */
	len1 = WebRtcIsac_Encode(st->inst, sampv,           (void *)buf);
	len2 = WebRtcIsac_Encode(st->inst, &sampv[sampc/2], (void *)buf);

	l = len1 ? len1 : len2;

	if (l > *len)
		return ENOMEM;

	*len = l;

	return 0;
}


static int decode(struct audec_state *st, int16_t *sampv,
		  size_t *sampc, const uint8_t *buf, size_t len)
{
	WebRtc_Word16 speechType;
	int n;

	if (!st || !sampv || !sampc || !buf || !len)
		return EINVAL;

	n = WebRtcIsac_Decode(st->inst, (void *)buf, len,
			      (void *)sampv, &speechType);
	if (n < 0)
		return EPROTO;

	if ((size_t)n > *sampc)
		return ENOMEM;

	*sampc = n;

	return 0;
}


static int plc(struct audec_state *st, int16_t *sampv, size_t *sampc)
{
	int n;

	if (!st || !sampv || !sampc)
		return EINVAL;

	n = WebRtcIsac_DecodePlc(st->inst, (void *)sampv, 1);
	if (n < 0)
		return EPROTO;

	*sampc = n;

	return 0;
}


static struct aucodec isacv[] = {
	{
	LE_INIT, 0, "isac", 32000, 32000, 1, 1, NULL,
	encode_update, encode, decode_update, decode, plc, NULL, NULL
	},
	{
	LE_INIT, 0, "isac", 16000, 16000, 1, 1, NULL,
	encode_update, encode, decode_update, decode, plc, NULL, NULL
	}
};


static int module_init(void)
{
	unsigned i;

	for (i=0; i<ARRAY_SIZE(isacv); i++)
		aucodec_register(baresip_aucodecl(), &isacv[i]);

	return 0;
}


static int module_close(void)
{
	int i = ARRAY_SIZE(isacv);

	while (i--)
		aucodec_unregister(&isacv[i]);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(isac) = {
	"isac",
	"codec",
	module_init,
	module_close
};
