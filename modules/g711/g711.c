/**
 * @file g711.c G.711 Audio Codec
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>


static int pcmu_encode(struct auenc_state *aes, uint8_t *buf,
		       size_t *len, const int16_t *sampv, size_t sampc)
{
	(void)aes;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < sampc)
		return ENOMEM;

	*len = sampc;

	while (sampc--)
		*buf++ = g711_pcm2ulaw(*sampv++);

	return 0;
}


static int pcmu_decode(struct audec_state *ads, int16_t *sampv,
		       size_t *sampc, const uint8_t *buf, size_t len)
{
	(void)ads;

	if (!sampv || !sampc || !buf)
		return EINVAL;

	if (*sampc < len)
		return ENOMEM;

	*sampc = len;

	while (len--)
		*sampv++ = g711_ulaw2pcm(*buf++);

	return 0;
}


static int pcma_encode(struct auenc_state *aes, uint8_t *buf,
		       size_t *len, const int16_t *sampv, size_t sampc)
{
	(void)aes;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < sampc)
		return ENOMEM;

	*len = sampc;

	while (sampc--)
		*buf++ = g711_pcm2alaw(*sampv++);

	return 0;
}


static int pcma_decode(struct audec_state *ads, int16_t *sampv,
		       size_t *sampc, const uint8_t *buf, size_t len)
{
	(void)ads;

	if (!sampv || !sampc || !buf)
		return EINVAL;

	if (*sampc < len)
		return ENOMEM;

	*sampc = len;

	while (len--)
		*sampv++ = g711_alaw2pcm(*buf++);

	return 0;
}


static struct aucodec pcmu = {
	LE_INIT, "0", "PCMU", 8000, 1, NULL,
	NULL, pcmu_encode, NULL, pcmu_decode, NULL, NULL, NULL
};

static struct aucodec pcma = {
	LE_INIT, "8", "PCMA", 8000, 1, NULL,
	NULL, pcma_encode, NULL, pcma_decode, NULL, NULL, NULL
};


static int module_init(void)
{
	aucodec_register(&pcmu);
	aucodec_register(&pcma);

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
