/**
 * @file bv32.c  BroadVoice32 audio codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <bv32/bv32.h>
#include <bv32/bitpack.h>


/*
 * BroadVoice32 Wideband Audio codec (RFC 4298)
 *
 * http://www.broadcom.com/support/broadvoice/downloads.php
 * http://files.freeswitch.org/downloads/libs/libbv32-0.1.tar.gz
 */


enum {
	NSAMP        = 80,
	CODED_OCTETS = 20
};


struct auenc_state {
	struct BV32_Encoder_State cs;
	struct BV32_Bit_Stream bsc;
};

struct audec_state {
	struct BV32_Decoder_State ds;
	struct BV32_Bit_Stream bsd;
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	Reset_BV32_Coder(&st->cs);
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	Reset_BV32_Decoder(&st->ds);
}


static int encode_update(struct auenc_state **aesp, const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	(void)prm;
	(void)fmtp;

	if (!aesp || !ac)
		return EINVAL;

	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	Reset_BV32_Coder(&st->cs);

	*aesp = st;

	return 0;
}


static int decode_update(struct audec_state **adsp,
			 const struct aucodec *ac, const char *fmtp)
{
	struct audec_state *st;
	(void)fmtp;

	if (!adsp || !ac)
		return EINVAL;

	if (*adsp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	Reset_BV32_Decoder(&st->ds);

	*adsp = st;

	return 0;
}


static int encode(struct auenc_state *st, uint8_t *buf, size_t *len,
		  int fmt, const void *sampv, size_t sampc)
{
	size_t i, nframe;
	short *p = (short *)sampv;

	nframe = sampc / NSAMP;

	if (*len < nframe * CODED_OCTETS)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	for (i=0; i<nframe; i++) {
		BV32_Encode(&st->bsc, &st->cs, &p[i*NSAMP]);
		BV32_BitPack((void *)&buf[i*CODED_OCTETS], &st->bsc);
	}

	*len = CODED_OCTETS * nframe;

	return 0;
}


static int decode(struct audec_state *st, int fmt, void *sampv,
		  size_t *sampc, const uint8_t *buf, size_t len)
{
	size_t i, nframe;
	short *p = sampv;

	nframe = len / CODED_OCTETS;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (*sampc < NSAMP*nframe)
		return ENOMEM;

	for (i=0; i<nframe; i++) {
		BV32_BitUnPack((void *)&buf[i*CODED_OCTETS], &st->bsd);
		BV32_Decode(&st->bsd, &st->ds, &p[i*NSAMP]);
	}

	*sampc = NSAMP * nframe;

	return 0;
}


static int plc(struct audec_state *st, int fmt, void *sampv, size_t *sampc)
{
	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	BV32_PLC(&st->ds, sampv);
	*sampc = NSAMP;

	return 0;
}


static struct aucodec bv32 = {
	LE_INIT, 0, "BV32", 16000, 16000, 1, 1, NULL,
	encode_update, encode,
	decode_update, decode, plc,
	NULL, NULL
};


static int module_init(void)
{
	aucodec_register(baresip_aucodecl(), &bv32);
	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&bv32);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(bv32) = {
	"bv32",
	"codec",
	module_init,
	module_close
};
