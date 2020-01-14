/**
 * @file g7221/decode.c G.722.1 Decode
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

#define G722_1_EXPOSE_INTERNAL_STRUCTURES

#include <g722_1.h>
#include "g7221.h"


struct audec_state {
	g722_1_decode_state_t dec;
};


int g7221_decode_update(struct audec_state **adsp, const struct aucodec *ac,
			const char *fmtp)
{
	const struct g7221_aucodec *g7221 = (struct g7221_aucodec *)ac;
	struct audec_state *ads;
	(void)fmtp;

	if (!adsp || !ac)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	ads = mem_alloc(sizeof(*ads), NULL);
	if (!ads)
		return ENOMEM;

	if (!g722_1_decode_init(&ads->dec, g7221->bitrate, ac->srate)) {
		mem_deref(ads);
		return EPROTO;
	}

	*adsp = ads;

	return 0;
}


int g7221_decode(struct audec_state *ads,
		 int fmt, void *sampv, size_t *sampc,
		 bool marker, const uint8_t *buf, size_t len)
{
	size_t framec;
	(void)marker;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	framec = len / ads->dec.bytes_per_frame;

	if (len != ads->dec.bytes_per_frame * framec)
		return EPROTO;

	if (*sampc < ads->dec.frame_size * framec)
		return ENOMEM;

	*sampc = g722_1_decode(&ads->dec, sampv, buf, (int)len);

	return 0;
}
