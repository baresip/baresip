/**
 * @file amr.c Adaptive Multi-Rate (AMR) audio codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <stdlib.h>
#ifdef AMR_NB
#include <interf_enc.h>
#include <interf_dec.h>
#endif
#ifdef AMR_WB
#ifdef _TYPEDEF_H
#define typedef_h
#endif
#include <enc_if.h>
#include <dec_if.h>
#endif
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "amr.h"


#ifdef VO_AMRWBENC_ENC_IF_H
#define IF2E_IF_encode E_IF_encode
#define IF2D_IF_decode D_IF_decode
#endif


/**
 * @defgroup amr amr
 *
 * This module supports both AMR Narrowband (8000 Hz) and
 * AMR Wideband (16000 Hz) audio codecs.
 *
 * NOTE: only octet-align mode is supported.
 *
 *
 * Reference:
 *
 *     http://tools.ietf.org/html/rfc4867
 *
 *     http://www.penguin.cz/~utx/amr
 */


#ifndef L_FRAME16k
#define L_FRAME16k 320
#endif

#ifndef NB_SERIAL_MAX
#define NB_SERIAL_MAX 61
#endif

enum {
	FRAMESIZE_NB = 160
};


struct auenc_state {
	const struct aucodec *ac;
	void *enc;                  /**< Encoder state            */
};

struct audec_state {
	const struct aucodec *ac;
	void *dec;                  /**< Decoder state            */
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	switch (st->ac->srate) {

#ifdef AMR_NB
	case 8000:
		Encoder_Interface_exit(st->enc);
		break;
#endif

#ifdef AMR_WB
	case 16000:
		E_IF_exit(st->enc);
		break;
#endif
	}
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	switch (st->ac->srate) {

#ifdef AMR_NB
	case 8000:
		Decoder_Interface_exit(st->dec);
		break;
#endif

#ifdef AMR_WB
	case 16000:
		D_IF_exit(st->dec);
		break;
#endif
	}
}


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

	st->ac = ac;

	switch (ac->srate) {

#ifdef AMR_NB
	case 8000:
		st->enc = Encoder_Interface_init(0);
		break;
#endif

#ifdef AMR_WB
	case 16000:
		st->enc = E_IF_init();
		break;
#endif
	}

	if (!st->enc)
		err = ENOMEM;

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

	st->ac = ac;

	switch (ac->srate) {

#ifdef AMR_NB
	case 8000:
		st->dec = Decoder_Interface_init();
		break;
#endif

#ifdef AMR_WB
	case 16000:
		st->dec = D_IF_init();
		break;
#endif
	}

	if (!st->dec)
		err = ENOMEM;

	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


#ifdef AMR_WB
static int encode_wb(struct auenc_state *st, uint8_t *buf, size_t *len,
		     int fmt, const void *sampv, size_t sampc)
{
	int n;

	if (sampc != L_FRAME16k)
		return EINVAL;

	if (*len < NB_SERIAL_MAX)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	/* CMR value 15 indicates that no mode request is present */
	buf[0] = 15 << 4;

	n = IF2E_IF_encode(st->enc, 8, sampv, &buf[1], 0);
	if (n <= 0)
		return EPROTO;

	*len = (1 + n);

	return 0;
}


static int decode_wb(struct audec_state *st,
		     int fmt, void *sampv, size_t *sampc,
		     const uint8_t *buf, size_t len)
{
	if (*sampc < L_FRAME16k)
		return ENOMEM;
	if (len > NB_SERIAL_MAX)
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	IF2D_IF_decode(st->dec, &buf[1], sampv, 0);

	*sampc = L_FRAME16k;

	return 0;
}
#endif


#ifdef AMR_NB
static int encode_nb(struct auenc_state *st, uint8_t *buf,
		     size_t *len, int fmt, const void *sampv, size_t sampc)
{
	int r;

	if (!st || !buf || !len || !sampv || sampc != FRAMESIZE_NB)
		return EINVAL;
	if (*len < NB_SERIAL_MAX)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	/* CMR value 15 indicates that no mode request is present */
	buf[0] = 15 << 4;

	r = Encoder_Interface_Encode(st->enc, MR122, sampv, &buf[1], 0);
	if (r <= 0)
		return EPROTO;

	*len = (1 + r);

	return 0;
}


static int decode_nb(struct audec_state *st, int fmt, void *sampv,
		     size_t *sampc, const uint8_t *buf, size_t len)
{
	if (!st || !sampv || !sampc || !buf)
		return EINVAL;

	if (len > NB_SERIAL_MAX)
		return EPROTO;

	if (*sampc < L_FRAME16k)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	Decoder_Interface_Decode(st->dec, &buf[1], sampv, 0);

	*sampc = FRAMESIZE_NB;

	return 0;
}
#endif


#ifdef AMR_WB
static struct aucodec amr_wb = {
	LE_INIT, NULL, "AMR-WB", 16000, 16000, 1, 1, NULL,
	encode_update, encode_wb,
	decode_update, decode_wb,
	NULL, amr_fmtp_enc, amr_fmtp_cmp
};
#endif
#ifdef AMR_NB
static struct aucodec amr_nb = {
	LE_INIT, NULL, "AMR", 8000, 8000, 1, 1, NULL,
	encode_update, encode_nb,
	decode_update, decode_nb,
	NULL, amr_fmtp_enc, amr_fmtp_cmp
};
#endif


static int module_init(void)
{
	int err = 0;

#ifdef AMR_WB
	aucodec_register(baresip_aucodecl(), &amr_wb);
#endif
#ifdef AMR_NB
	aucodec_register(baresip_aucodecl(), &amr_nb);
#endif

	return err;
}


static int module_close(void)
{
#ifdef AMR_WB
	aucodec_unregister(&amr_wb);
#endif
#ifdef AMR_NB
	aucodec_unregister(&amr_nb);
#endif

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(amr) = {
	"amr",
	"codec",
	module_init,
	module_close
};
