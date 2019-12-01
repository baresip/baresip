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
 * The CODEC2 low-bitrate speech audio codec
 *
 * https://en.wikipedia.org/wiki/Codec2
 */


struct auenc_state {
	struct CODEC2 *c2;
};

struct audec_state {
	struct CODEC2 *c2;
};


static uint32_t codec2_mode = CODEC2_MODE_2400;


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

	st->c2 = codec2_create(codec2_mode);
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

	st->c2 = codec2_create(codec2_mode);
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


static int encode(struct auenc_state *aes, bool *marker, uint8_t *buf,
		  size_t *len, int fmt, const void *sampv, size_t sampc)
{
	size_t bytes_per_frame;
	(void)marker;

	if (!buf || !len || !sampv)
		return EINVAL;

	bytes_per_frame = (codec2_bits_per_frame(aes->c2) + 7) / 8;

	if (*len < bytes_per_frame)
		return ENOMEM;
	if (sampc != (size_t)codec2_samples_per_frame(aes->c2))
		return EPROTO;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	codec2_encode(aes->c2, buf, (short *)sampv);

	*len = bytes_per_frame;

	return 0;
}


static int decode(struct audec_state *ads, int fmt, void *sampv,
		  size_t *sampc, bool marker, const uint8_t *buf, size_t len)
{
	size_t bytes_per_frame;
	(void)marker;

	if (!sampv || !sampc || !buf)
		return EINVAL;

	bytes_per_frame = (codec2_bits_per_frame(ads->c2) + 7) / 8;

	if (*sampc < (size_t)codec2_samples_per_frame(ads->c2))
		return ENOMEM;
	if (len < bytes_per_frame)
		return EPROTO;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	codec2_decode(ads->c2, sampv, buf);

	*sampc = codec2_samples_per_frame(ads->c2);

	return 0;
}


static struct aucodec codec2 = {
	.name      = "CODEC2",
	.srate     = 8000,
	.crate     = 8000,
	.ch        = 1,
	.pch       = 1,
	.encupdh   = encode_update,
	.ench      = encode,
	.decupdh   = decode_update,
	.dech      = decode,
};


static int module_init(void)
{
	conf_get_u32(conf_cur(), "codec2_mode", &codec2_mode);

	info("codec2: using mode %d\n", codec2_mode);

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
