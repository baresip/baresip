/**
 * @file g729.c G.729 Audio Codec
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "bcg729/encoder.h"
#include "bcg729/decoder.h"

/**
 * @defgroup g729 g729
 *
 * The G.729 audio codec
 */

enum {
	FRAME_SIZE = 160
};

struct auenc_state {
	struct aucodec ac;
	bcg729EncoderChannelContextStruct *encoder_object;
};

struct audec_state {
	struct aucodec ac;
	bcg729DecoderChannelContextStruct *decoder_object;
};

static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	closeBcg729EncoderChannel(st->encoder_object);
	st->encoder_object = NULL;
}

static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	closeBcg729DecoderChannel(st->decoder_object);
	st->decoder_object = NULL;
}

static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;

	if (!aesp || !ac)
		return EINVAL;

	/* Already open? */
	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	/* For the moment only 729A */
	st->encoder_object = initBcg729EncoderChannel(0);

	*aesp = st;

	return 0;
}

static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct audec_state *st;

	if (!adsp || !ac)
		return EINVAL;
	if (*adsp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->decoder_object = initBcg729DecoderChannel();

	*adsp = st;

	return 0;
}

static int g729_encode(struct auenc_state *aes, uint8_t *buf,
				size_t *len, int fmt,
				const void *sampv, size_t sampc)
{
	if (!len || !aes) {
		warning("g729_encode: !len or !aes");
		return EINVAL;
	}

	if (fmt != AUFMT_S16LE) {
		warning("g729_encode: !AUFMT_S16LE");
		return ENOTSUP;
	}

	if (sampc % FRAME_SIZE == 0) {
		int x;
		uint16_t new_len = 0;
		int16_t *ddp = (int16_t *) sampv;
		uint8_t *edp = buf;

		int loops = (int) sampc / FRAME_SIZE;

		for (x = 0; x < loops && new_len < *len; x++) {
			uint8_t frameSize;

			bcg729Encoder(
				aes->encoder_object,
				ddp,
				edp,
				&frameSize
			);
			edp += frameSize;
			ddp += frameSize * 8;
			new_len += frameSize;
		}

		/*info("g729_encode %u %u %u\n", sampc, *len, new_len);*/

		if (new_len <= *len) {
			*len = new_len;
		}
		else {
			warning("buffer overflow!!! %u >= %u\n",
				new_len, *len);
			return ENOMEM;
		}
	}

	return 0;
}

static int g729_decode(struct audec_state *ads, int fmt, void *sampv,
			size_t *sampc, const uint8_t *buf, size_t len)
{
	int framesize;
	unsigned int x;
	uint32_t new_len = 0;
	const uint8_t *edp = buf;
	int16_t *ddp = sampv;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (len == 0) {  /* Native PLC interpolation */
		bcg729Decoder(ads->decoder_object, NULL, 0, 1, 0, 0, ddp);
		ddp += 80;
		*sampc = FRAME_SIZE;
		warning("g729 zero length frame\n");
		return 0;
	}

	for (x = 0; x < len && new_len < *sampc; x += framesize) {
		uint8_t isSID = (len - x < 8) ? 1 : 0;
		framesize = (isSID==1) ? 2 : 10;

		bcg729Decoder(ads->decoder_object,
			(uint8_t *) edp,
			*sampc,
			0,
			isSID,
			0,
			ddp
		);
		ddp += 80;
		edp += framesize;
		new_len += FRAME_SIZE;
	}

	/*info("g729_decode %u %u %u\n", *sampc, len, new_len);*/

	if (new_len <= *sampc) {
		*sampc = new_len;
	}
	else {
		warning("buffer overflow!!!\n");
		return ENOMEM;
	}

	return 0;
}

static int g729_fmtp_enc(struct mbuf *mb, const struct sdp_format *fmt,
		 bool offer, void *arg)
{
	const struct aucodec *ac = arg;
	(void)offer;

	if (!mb || !fmt || !ac)
		return 0;

	/* For the moment only 729A */
	return mbuf_printf(mb, "a=fmtp:%s annexb=no\r\n",
			   fmt->id);
}

static struct aucodec ac_g729 = {
	LE_INIT, "18", "G729", 8000, 8000, 1, 1, NULL,
	encode_update, g729_encode, decode_update, g729_decode,
	NULL, g729_fmtp_enc, NULL
};

static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), &ac_g729);

	return 0;
}

static int module_close(void)
{
	aucodec_unregister(&ac_g729);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(g729) = {
	"g729",
	"audio codec",
	module_init,
	module_close,
};
