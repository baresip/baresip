/**
 * @file libg722.c  G.722 audio codec using libg722
 *
 * Copyright (C) 2025
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <re.h>
#include <rem_au.h>
#include <baresip.h>

#ifndef USE_LIBG722
#error "The libg722 module requires libg722"
#endif

#include <g722_codec.h>

/* Forward declarations */
static void encode_destructor(void *arg);
static void decode_destructor(void *arg);

enum {
	G722_SAMPLE_RATE = 16000,
	G722_BITRATE_48k = 48000,
	G722_BITRATE_56k = 56000,
	G722_BITRATE_64k = 64000
};


struct auenc_state {
	G722_ENC_CTX *enc;
};

struct audec_state {
	G722_DEC_CTX *dec;
};


static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	int err = 0;
	(void)prm;
	(void)fmtp;

	if (!aesp || !ac)
		return EINVAL;

	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->enc = g722_encoder_new(G722_BITRATE_64k, 0);
	if (!st->enc) {
		err = EPROTO;
		goto out;
	}

out:
	if (err)
		mem_deref(st);
	else
		*aesp = st;

	return err;
}


static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct audec_state *st;
	int err = 0;
	(void)fmtp;

	if (!adsp || !ac)
		return EINVAL;

	if (*adsp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->dec = g722_decoder_new(G722_BITRATE_64k, 0);
	if (!st->dec) {
		err = EPROTO;
		goto out;
	}

out:
	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


static int encode(struct auenc_state *st,
		  bool *marker, uint8_t *buf, size_t *len,
		  int fmt, const void *sampv, size_t sampc)
{
	int n;
	(void)marker;

	if (!st)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (!st->enc)
		return EINVAL;

	n = g722_encode(st->enc, (const int16_t *)sampv, (int)sampc, buf);

	if (n <= 0) {
		return EPROTO;
	}
	else if (n > (int)*len) {
		return EOVERFLOW;
	}

	*len = n;

	return 0;
}


static int decode(struct audec_state *st, int fmt, void *sampv, size_t *sampc,
		  bool marker, const uint8_t *buf, size_t len)
{
	int n;
	(void)marker;

	if (!st || !sampv || !buf)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (!st->dec)
		return EINVAL;

	n = g722_decode(st->dec, buf, (int)len, (int16_t *)sampv);

	if (n < 0)
		return EPROTO;

	*sampc = n;

	return 0;
}


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;
	if (!st)
		return;

	if (st->enc) {
		g722_encoder_destroy(st->enc);
		st->enc = NULL;
	}
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;
	if (!st)
		return;

	if (st->dec) {
		g722_decoder_destroy(st->dec);
		st->dec = NULL;
	}
}


static struct aucodec g722 = {
	.pt      = "9",
	.name    = "G722",
	.srate   = 16000,
	.crate   = 8000,
	.ch      = 1,
	.pch     = 1,
	.encupdh = encode_update,
	.ench    = encode,
	.decupdh = decode_update,
	.dech    = decode,
};

static int module_init(void)
{
	info("libg722: using libg722 library\n");

	aucodec_register(baresip_aucodecl(), &g722);
	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&g722);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(libg722) = {
	"libg722",
	"codec",
	module_init,
	module_close
};
