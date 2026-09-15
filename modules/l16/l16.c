/**
 * @file l16.c  16-bit linear codec
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup l16 l16
 *
 * Linear 16-bit audio codec
 */


static int encode(struct auenc_state *st,
		  bool *marker, uint8_t *buf, size_t *len,
		  int fmt, const void *sampv, size_t sampc)
{
	int16_t *p = (void *)buf;
	const int16_t *sampv16 = sampv;
	(void)st;
	(void)marker;

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
		  bool marker, const uint8_t *buf, size_t len)
{
	int16_t *p = (void *)buf;
	int16_t *sampv16 = sampv;
	(void)st;
	(void)marker;

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


/* ptime cap: keep the payload within 1400 bytes (1500 MTU - hdrs) */
#define L16_FMT(srate, ch) srate, srate, ch, ch, 1400*1000/((srate)*(ch)*2)


/* See RFC 3551 */
static struct aucodec l16v[] = {
{LE_INIT,    0, "L16", L16_FMT(48000, 2), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT, "10", "L16", L16_FMT(44100, 2), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT,    0, "L16", L16_FMT(32000, 2), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT,    0, "L16", L16_FMT(16000, 2), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT,    0, "L16", L16_FMT( 8000, 2), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT,    0, "L16", L16_FMT(48000, 1), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT, "11", "L16", L16_FMT(44100, 1), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT,    0, "L16", L16_FMT(32000, 1), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT,    0, "L16", L16_FMT(16000, 1), 0, 0, encode, 0, decode, 0,0,0},
{LE_INIT,    0, "L16", L16_FMT( 8000, 1), 0, 0, encode, 0, decode, 0,0,0},
};


static int module_init(void)
{
	struct list *aucodecl = baresip_aucodecl();

	for (size_t i = 0; i < RE_ARRAY_SIZE(l16v); i++)
		aucodec_register(aucodecl, &l16v[i]);

	return 0;
}


static int module_close(void)
{
	for (size_t i = 0; i < RE_ARRAY_SIZE(l16v); i++)
		aucodec_unregister(&l16v[i]);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(l16) = {
	"l16",
	"codec",
	module_init,
	module_close
};
