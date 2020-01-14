/**
 * @file mock/mock_aucodec.c Mock audio codec
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "../test.h"


static int mock_raw_encode(struct auenc_state *st,
			   bool *marker, uint8_t *buf, size_t *len,
			   int fmt, const void *sampv, size_t sampc)
{
	const size_t sampsz = aufmt_sample_size(fmt);
	size_t bytes;
	(void)st;
	(void)marker;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (sampsz == 0)
		return ENOTSUP;

	bytes = sampc * sampsz;
	if (bytes > *len)
		return ENOMEM;

	memcpy(buf, sampv, bytes);
	*len = bytes;

	return 0;
}


static int mock_raw_decode(struct audec_state *st,
			   int fmt, void *sampv, size_t *sampc,
			   bool marker, const uint8_t *buf, size_t len)
{
	const size_t sampsz = aufmt_sample_size(fmt);
	(void)st;
	(void)marker;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (sampsz == 0)
		return ENOTSUP;

	if (len / sampsz > *sampc)
		return ENOMEM;

	memcpy(sampv, buf, len);
	*sampc = len / sampsz;

	return 0;
}


static struct aucodec ac_dummy = {
	.name = "RAW-CODEC",
	.srate = 8000,
	.crate = 8000,
	.ch  = 1,
	.pch = 1,
	.ench = mock_raw_encode,
	.dech = mock_raw_decode,
};


void mock_aucodec_register(struct list *aucodecl)
{
	aucodec_register(aucodecl, &ac_dummy);
}


void mock_aucodec_unregister(void)
{
	aucodec_unregister(&ac_dummy);
}
