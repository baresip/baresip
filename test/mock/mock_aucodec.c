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


/* A dummy protocol header */
#define L16_HEADER 0x1616


static int mock_l16_encode(struct auenc_state *st, uint8_t *buf, size_t *len,
			   int fmt, const void *sampv_void, size_t sampc)
{
	int16_t *p = (void *)buf;
	const int16_t *sampv = sampv_void;
	(void)st;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (*len < sampc*2)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*len = 2 + sampc*2;

	*p++ = L16_HEADER;

	while (sampc--)
		*p++ = htons(*sampv++);

	return 0;
}


static int mock_l16_decode(struct audec_state *st,
			   int fmt, void *sampv_void, size_t *sampc,
			   const uint8_t *buf, size_t len)
{
	int16_t *p = (void *)buf;
	int16_t *sampv = sampv_void;
	uint16_t hdr;
	(void)st;

	if (!buf || !len || !sampv)
		return EINVAL;

	if (len < 2)
		return EINVAL;

	if (*sampc < len/2)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	*sampc = (len - 2)/2;

	hdr = *p++;
	if (L16_HEADER != hdr) {
		warning("mock_aucodec: invalid L16 header"
			" 0x%04x (len=%zu)\n", hdr, len);
		return EPROTO;
	}

	len = len/2 - 2;
	while (len--)
		*sampv++ = ntohs(*p++);

	return 0;
}


static struct aucodec ac_dummy = {
	.name = "FOO16",
	.srate = 8000,
	.crate = 8000,
	.ch  = 1,
	.pch = 1,
	.ench = mock_l16_encode,
	.dech = mock_l16_decode,
};


void mock_aucodec_register(void)
{
	aucodec_register(baresip_aucodecl(), &ac_dummy);
}


void mock_aucodec_unregister(void)
{
	aucodec_unregister(&ac_dummy);
}
