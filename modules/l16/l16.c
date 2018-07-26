/**
 * @file l16.c  16-bit linear codec
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup l16 l16
 *
 * Linear 16-bit audio codec
 */


enum {NR_CODECS = 8};


static int encode(struct auenc_state *st, uint8_t *buf, size_t *len,
		  int fmt, const void *sampv, size_t sampc)
{
	int16_t *p = (void *)buf;
	const int16_t *sampv16 = sampv;
	(void)st;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < sampc*2)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*len = sampc*2;

	while (sampc--)
		*p++ = htons(*sampv16++);

	return 0;
}


static int decode(struct audec_state *st, int fmt, void *sampv, size_t *sampc,
		  const uint8_t *buf, size_t len)
{
	int16_t *p = (void *)buf;
	int16_t *sampv16 = sampv;
	(void)st;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*sampc < len/2)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*sampc = len/2;

	len /= 2;
	while (len--)
		*sampv16++ = ntohs(*p++);

	return 0;
}


/* See RFC 3551 */
static struct aucodec l16v[NR_CODECS] = {
{LE_INIT, "10", "L16", 44100, 44100, 2, 2, 0, 0, encode, 0, decode, 0, 0, 0},
{LE_INIT,    0, "L16", 32000, 32000, 2, 2, 0, 0, encode, 0, decode, 0, 0, 0},
{LE_INIT,    0, "L16", 16000, 16000, 2, 2, 0, 0, encode, 0, decode, 0, 0, 0},
{LE_INIT,    0, "L16",  8000,  8000, 2, 2, 0, 0, encode, 0, decode, 0, 0, 0},
{LE_INIT, "11", "L16", 44100, 44100, 1, 1, 0, 0, encode, 0, decode, 0, 0, 0},
{LE_INIT,    0, "L16", 32000, 32000, 1, 1, 0, 0, encode, 0, decode, 0, 0, 0},
{LE_INIT,    0, "L16", 16000, 16000, 1, 1, 0, 0, encode, 0, decode, 0, 0, 0},
{LE_INIT,    0, "L16",  8000,  8000, 1, 1, 0, 0, encode, 0, decode, 0, 0, 0},
};


static int module_init(void)
{
	struct list *aucodecl = baresip_aucodecl();
	size_t i;

	for (i=0; i<NR_CODECS; i++)
		aucodec_register(aucodecl, &l16v[i]);

	return 0;
}


static int module_close(void)
{
	size_t i;

	for (i=0; i<NR_CODECS; i++)
		aucodec_unregister(&l16v[i]);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(l16) = {
	"l16",
	"codec",
	module_init,
	module_close
};
