/**
 * @file codec2.c  CODEC2 audio codec
 *
 * Copyright (C) 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <codec2/codec2.h>


/**
 * @defgroup codec2 codec2
 *
 * The CODEC2 audio codec
 *
 * https://en.wikipedia.org/wiki/Codec2
 */


enum {
	CODEC2_MODE = CODEC2_MODE_2400
};


struct auenc_state {
	struct CODEC2 *c2;
};

struct audec_state {
	struct CODEC2 *c2;
};


static void encode_destructor(void *data)
{
	struct auenc_state *st = data;

	if (st->c2)
		codec2_destroy(st->c2);
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

	st->c2 = codec2_create(CODEC2_MODE);
	if (!st->c2) {
		err = ENOMEM;
		goto out;
	}

	info("codec2: %d samples per frame, %d bits per frame\n",
	     codec2_samples_per_frame(st->c2),
	     codec2_bits_per_frame(st->c2));

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

	if (st->c2)
		codec2_destroy(st->c2);
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

	st->c2 = codec2_create(CODEC2_MODE);
	if (!st->c2) {
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


static int encode(struct auenc_state *aes, uint8_t *buf,
		  size_t *len, int fmt, const void *sampv, size_t sampc)
{
	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < (size_t)codec2_bits_per_frame(aes->c2)/8)
		return ENOMEM;
	if (sampc != (size_t)codec2_samples_per_frame(aes->c2))
		return EPROTO;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	codec2_encode(aes->c2, buf, (short *)sampv);

	*len = codec2_bits_per_frame(aes->c2)/8;

	return 0;
}


static int decode(struct audec_state *ads, int fmt, void *sampv,
		  size_t *sampc, const uint8_t *buf, size_t len)
{
	if (!sampv || !sampc || !buf)
		return EINVAL;

	if (*sampc < (size_t)codec2_samples_per_frame(ads->c2))
		return ENOMEM;
	if (len < (size_t)codec2_bits_per_frame(ads->c2)/8)
		return EPROTO;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	codec2_decode(ads->c2, sampv, buf);

	*sampc = codec2_samples_per_frame(ads->c2);

	return 0;
}


static struct aucodec codec2 = {
	LE_INIT,
	NULL,
	"CODEC2",
	8000,
	8000,
	1,
	1,
	NULL,
	encode_update,
	encode,
	decode_update,
	decode,
	NULL,
	NULL,
	NULL
};


static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), &codec2);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&codec2);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(codec2) = {
	"codec2",
	"audio codec",
	module_init,
	module_close,
};
