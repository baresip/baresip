/**
 * @file aptx/decode.c aptX Decoder
 *
 * Copyright (C) 2019 Hessischer Rundfunk
 */

#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <openaptx.h>
#include "aptx.h"

struct audec_state {
	struct aptx_context *dec;
};


static void destructor(void *arg)
{
	struct audec_state *ads = arg;

	if (ads->dec)
		aptx_finish(ads->dec);
}


int aptx_decode_update(struct audec_state **adsp, const struct aucodec *ac,
                       const char *fmtp)
{
	struct audec_state *ads;
	int err = 0;

	(void)fmtp;

	if (!adsp || !ac || !ac->ch)
		return EINVAL;

	ads = *adsp;

	if (ads)
		return 0;

	ads = mem_zalloc(sizeof(*ads), destructor);
	if (!ads)
		return ENOMEM;

	ads->dec = aptx_init(APTX_VARIANT);
	if (!ads->dec) {
		warning("aptx: Cannot initialize decoder.\n");
		err = ENOMEM;
		goto out;
	}

out:
	if (err)
		mem_deref(ads);
	else
		*adsp = ads;

	return err;
}


int aptx_decode_frm(struct audec_state *ads, int fmt, void *sampv,
                    size_t *sampc, bool marker, const uint8_t *buf,
                    size_t len)
{
	size_t processed = 0;
	size_t written = 0;

	uint8_t *sampv_buf = sampv;

	(void)marker;

	if (!ads || !sampv || !sampc || !buf)
		return EINVAL;

	processed = aptx_decode(ads->dec, buf, len, sampv, *sampc, &written);

	*sampc = written / APTX_WORDSIZE;

	if (written == 0)
		return 0;

	if (processed != len)
		warning("aptx: Decoding stopped in the middle of the sample, "
		        "dropped %u bytes\n",
		        (unsigned int)(len - processed));

	switch (fmt) {

	case AUFMT_S16LE:
		/* remap S24 to S16 in same buffer */
		for (size_t s = 0; s < *sampc; s++) {
			sampv_buf[s * 2] = sampv_buf[(s * APTX_WORDSIZE) + 1];
			sampv_buf[s * 2 + 1] =
			    sampv_buf[(s * APTX_WORDSIZE) + 2];
		}
		break;

	case AUFMT_S24_3LE:
		break;

	default:
		return ENOTSUP;
	}

	return 0;
}
