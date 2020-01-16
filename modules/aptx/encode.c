/**
 * @file aptx/encode.c aptX Encoder
 *
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <openaptx.h>
#include "aptx.h"

struct auenc_state {
	struct aptx_context *enc;
};


static void destructor(void *arg)
{
	struct auenc_state *aes = arg;

	if (aes->enc)
		aptx_finish(aes->enc);
}


int aptx_encode_update(struct auenc_state **aesp, const struct aucodec *ac,
                       struct auenc_param *param, const char *fmtp)
{
	struct auenc_state *aes;
	int err = 0;

	(void)param;
	(void)fmtp;

	if (!aesp || !ac || !ac->ch)
		return EINVAL;

	aes = *aesp;

	if (aes)
		goto out;

	aes = mem_zalloc(sizeof(*aes), destructor);
	if (!aes)
		return ENOMEM;

	aes->enc = aptx_init(APTX_VARIANT);
	if (!aes->enc) {
		warning("aptx: Cannot initialize encoder.\n");
		err = ENOMEM;
		goto out;
	}

	*aesp = aes;

out:
	if (err)
		mem_deref(aes);

	return err;
}


int aptx_encode_frm(struct auenc_state *aes, bool *marker, uint8_t *buf,
                    size_t *len, int fmt, const void *sampv, size_t sampc)
{
	size_t processed = 0;
	size_t written = 0;

	uint8_t *intermediate_buf;
	size_t sampv_len;

	const uint8_t *sampv_buf = sampv;

	(void)marker;

	if (!aes || !buf || !len || !sampv)
		return EINVAL;

	sampv_len = sampc * APTX_WORDSIZE;

	switch (fmt) {

	case AUFMT_S16LE:
		intermediate_buf = mem_alloc(sampv_len, NULL);

		if (!intermediate_buf)
			return ENOMEM;

		/* map S16 to S24 intermediate buffer */
		for (size_t s = 0; s < sampc; s++) {
			intermediate_buf[s * APTX_WORDSIZE] = 0;
			intermediate_buf[s * APTX_WORDSIZE + 1] =
			    sampv_buf[s * 2];
			intermediate_buf[s * APTX_WORDSIZE + 2] =
			    sampv_buf[s * 2 + 1];
		}

		processed = aptx_encode(aes->enc, intermediate_buf, sampv_len,
		                        buf, *len, &written);

		mem_deref(intermediate_buf);
		break;

	case AUFMT_S24_3LE:
		processed = aptx_encode(aes->enc, sampv_buf, sampv_len, buf,
		                        *len, &written);
		break;

	default:
		return ENOTSUP;
	}

	if (processed != sampv_len)
		warning("aptx: Encoding stopped in the middle of the sample, "
		        "dropped %u bytes\n",
		        (unsigned int)(sampv_len - processed));

	*len = written;

	return 0;
}
