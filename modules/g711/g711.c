/**
 * @file g711.c G.711 Audio Codec
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup g711 g711
 *
 * The G.711 audio codec
 */


static int pcmu_encode(struct auenc_state *aes, bool *marker, uint8_t *buf,
		       size_t *len, int fmt, const void *sampv, size_t sampc)
{
	const int16_t *p = sampv;

	(void)aes;
	(void)marker;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < sampc)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*len = sampc;

	while (sampc--)
		*buf++ = g711_pcm2ulaw(*p++);

	return 0;
}


static int pcmu_decode(struct audec_state *ads, int fmt, void *sampv,
		       size_t *sampc, bool marker,
		       const uint8_t *buf, size_t len)
{
	int16_t *p = sampv;

	(void)ads;
	(void)marker;

	if (!sampv || !sampc || !buf)
		return EINVAL;

	if (*sampc < len)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*sampc = len;

	while (len--)
		*p++ = g711_ulaw2pcm(*buf++);

	return 0;
}


static int pcma_encode(struct auenc_state *aes, bool *marker, uint8_t *buf,
		       size_t *len, int fmt, const void *sampv, size_t sampc)
{
	const int16_t *p = sampv;

	(void)aes;
	(void)marker;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < sampc)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*len = sampc;

	while (sampc--)
		*buf++ = g711_pcm2alaw(*p++);

	return 0;
}


static int pcma_decode(struct audec_state *ads, int fmt, void *sampv,
		       size_t *sampc, bool marker,
		       const uint8_t *buf, size_t len)
{
	int16_t *p = sampv;

	(void)ads;
	(void)marker;

	if (!sampv || !sampc || !buf)
		return EINVAL;

	if (*sampc < len)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*sampc = len;

	while (len--)
		*p++ = g711_alaw2pcm(*buf++);

	return 0;
}


static struct aucodec pcmu = {
	.pt    = "0",
	.name  = "PCMU",
	.srate = 8000,
	.crate = 8000,
	.ch    = 1,
	.pch   = 1,
	.ench  = pcmu_encode,
	.dech  = pcmu_decode,
};

static struct aucodec pcma = {
	.pt    = "8",
	.name  = "PCMA",
	.srate = 8000,
	.crate = 8000,
	.ch    = 1,
	.pch   = 1,
	.ench  = pcma_encode,
	.dech  = pcma_decode,
};


static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), &pcmu);
	aucodec_register(baresip_aucodecl(), &pcma);

	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&pcma);
	aucodec_unregister(&pcmu);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(g711) = {
	"g711",
	"audio codec",
	module_init,
	module_close,
};
