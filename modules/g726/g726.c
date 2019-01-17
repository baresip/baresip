/**
 * @file g726.c G.726 Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem_au.h>
#include <baresip.h>
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES 1
#include <spandsp.h>


/**
 * @defgroup g726 g726
 *
 * The G.726 audio codec
 */


enum { MAX_PACKET = 100 };


struct g726_aucodec {
	struct aucodec ac;
	int bitrate;
};

struct auenc_state {
	g726_state_t st;
};

struct audec_state {
	g726_state_t st;
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	g726_release(&st->st);
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	g726_release(&st->st);
}


static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct g726_aucodec *gac = (struct g726_aucodec *)ac;
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

	if (!g726_init(&st->st, gac->bitrate, G726_ENCODING_LINEAR,
		       G726_PACKING_LEFT)) {
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


static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct g726_aucodec *gac = (struct g726_aucodec *)ac;
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

	if (!g726_init(&st->st, gac->bitrate, G726_ENCODING_LINEAR,
		       G726_PACKING_LEFT)) {
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


static int encode(struct auenc_state *st, uint8_t *buf,
		  size_t *len, int fmt, const void *sampv, size_t sampc)
{
	if (!buf || !len || !sampv)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (*len < MAX_PACKET)
		return ENOMEM;

	*len = g726_encode(&st->st, buf, sampv, (int)sampc);

	return 0;
}


static int decode(struct audec_state *st, int fmt, void *sampv,
		  size_t *sampc, const uint8_t *buf, size_t len)
{
	if (!sampv || !sampc || !buf)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*sampc = g726_decode(&st->st, sampv, buf, (int)len);

	return 0;
}


static struct g726_aucodec g726[4] = {
	{
		{
			LE_INIT, 0, "G726-40", 8000, 8000, 1, 1, NULL,
			encode_update, encode, decode_update, decode, 0, 0, 0
		},
		40000
	},
	{
		{
			LE_INIT, 0, "G726-32", 8000, 8000, 1, 1, NULL,
			encode_update, encode, decode_update, decode, 0, 0, 0
		},
		32000
	},
	{
		{
			LE_INIT, 0, "G726-24", 8000, 8000, 1, 1, NULL,
			encode_update, encode, decode_update, decode, 0, 0, 0
		},
		24000
	},
	{
		{
			LE_INIT, 0, "G726-16", 8000, 8000, 1, 1, NULL,
			encode_update, encode, decode_update, decode, 0, 0, 0
		},
		16000
	}
};


static int module_init(void)
{
	struct list *aucodecl = baresip_aucodecl();
	size_t i;

	for (i=0; i<ARRAY_SIZE(g726); i++)
		aucodec_register(aucodecl, (struct aucodec *)&g726[i]);

	return 0;
}


static int module_close(void)
{
	size_t i;

	for (i=0; i<ARRAY_SIZE(g726); i++)
		aucodec_unregister((struct aucodec *)&g726[i]);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(g726) = {
	"g726",
	"audio codec",
	module_init,
	module_close,
};
