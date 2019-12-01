/**
 * @file g7221/encode.c G.722.1 Encode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#define G722_1_EXPOSE_INTERNAL_STRUCTURES

#include <g722_1.h>
#include "g7221.h"


struct auenc_state {
	g722_1_encode_state_t enc;
};


int g7221_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
			struct auenc_param *prm, const char *fmtp)
{
	const struct g7221_aucodec *g7221 = (struct g7221_aucodec *)ac;
	struct auenc_state *aes;
	(void)prm;
	(void)fmtp;

	if (!aesp || !ac)
		return EINVAL;

	aes = *aesp;

	if (aes)
		return 0;

	aes = mem_alloc(sizeof(*aes), NULL);
	if (!aes)
		return ENOMEM;

	if (!g722_1_encode_init(&aes->enc, g7221->bitrate, ac->srate)) {
		mem_deref(aes);
		return EPROTO;
	}

	*aesp = aes;

	return 0;
}


int g7221_encode(struct auenc_state *aes,
		 bool *marker, uint8_t *buf, size_t *len,
		 int fmt, const void *sampv, size_t sampc)
{
	size_t framec;
	(void)marker;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	framec = sampc / aes->enc.frame_size;

	if (sampc != aes->enc.frame_size * framec)
		return EPROTO;

	if (*len < aes->enc.bytes_per_frame * framec)
		return ENOMEM;

	*len = g722_1_encode(&aes->enc, buf, sampv, (int)sampc);

	return 0;
}
