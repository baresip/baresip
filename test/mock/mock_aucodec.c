/**
 * @file mock_aucodec.c mockup for audio codec with float support
 *
 * Copyright (C) 2024 commend.com - Christian Spielberger
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <string.h>
#include "../test.h"


static int encode(struct auenc_state *aes, bool *marker, uint8_t *buf,
		       size_t *len, int fmt, const void *sampv, size_t sampc)
{
	const uint8_t *p = sampv;
	size_t sz = aufmt_sample_size(fmt);
	size_t psize = sampc * sz;

	(void)aes;
	(void)marker;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < psize)
		return ENOMEM;

	*len = psize;

	while (sampc--) {
		/* raw format */
		memcpy(buf, p, sz);

		buf += sz;
		p += sz;
	}

	return 0;
}


static int decode(struct audec_state *ads, int fmt, void *sampv,
		       size_t *sampc, bool marker,
		       const uint8_t *buf, size_t len)
{
	uint8_t *p = sampv;
	size_t sz = aufmt_sample_size(fmt);

	(void)ads;
	(void)marker;

	if (!sampv || !sampc || !buf || !len)
		return EINVAL;

	if (*sampc < len)
		return ENOMEM;

	*sampc = len/sz;

	while (len) {
		memcpy(p, buf, sz);
		buf += sz;
		p += sz;
		len -= sz;
	}

	return 0;
}


static struct aucodec aucmock = {
	.name  = "aucmock",
	.srate = 48000,
	.crate = 48000,
	.ch    = 2,
	.pch   = 2,
	.ench  = encode,
	.dech  = decode,
};


void mock_aucodec_register(void)
{
	aucodec_register(baresip_aucodecl(), &aucmock);
}


void mock_aucodec_unregister(void)
{
	aucodec_unregister(&aucmock);
}
