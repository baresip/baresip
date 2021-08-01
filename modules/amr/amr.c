/**
 * @file amr.c Adaptive Multi-Rate (AMR) audio codec
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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
	const struct amr_aucodec *ac;
	void *enc;                  /**< Encoder state            */
};

struct audec_state {
	const struct amr_aucodec *ac;
	void *dec;                  /**< Decoder state            */
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	switch (st->ac->ac.srate) {

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
	struct amr_aucodec *amr_ac = (struct amr_aucodec *)st->ac;

	switch (st->ac->ac.srate) {

#ifdef AMR_NB
	case 8000:
		Decoder_Interface_exit(st->dec);

		mem_deref(amr_ac->be_dec_arr);

		break;
#endif

#ifdef AMR_WB
	case 16000:
		D_IF_exit(st->dec);

		mem_deref(amr_ac->be_dec_arr);

		break;
#endif
	}
}


static int encode_update(struct auenc_state **aesp,
			 const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct amr_aucodec *amr_ac = (struct amr_aucodec *)ac;
	struct auenc_state *st;
	int err = 0;
	(void)prm;

	if (!aesp || !ac)
		return EINVAL;

	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->ac = amr_ac;
	amr_ac->aligned = amr_octet_align(fmtp);

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
	struct amr_aucodec *amr_ac = (struct amr_aucodec *)ac;
	struct audec_state *st;
	int err = 0;

	if (!adsp || !ac)
		return EINVAL;

	if (*adsp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->ac = amr_ac;
	amr_ac->aligned = amr_octet_align(fmtp);

	switch (ac->srate) {

#ifdef AMR_NB
	case 8000:
		st->dec = Decoder_Interface_init();

		if (!amr_ac->aligned) {
			amr_ac->be_dec_arr = mem_zalloc(NB_SERIAL_MAX, NULL);

			if (!amr_ac->be_dec_arr)
				err = ENOMEM;
		}
		break;
#endif

#ifdef AMR_WB
	case 16000:
		st->dec = D_IF_init();

		if (!amr_ac->aligned) {
			amr_ac->be_dec_arr = mem_zalloc(1+NB_SERIAL_MAX, NULL);

			if (!amr_ac->be_dec_arr)
				err = ENOMEM;
		}
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

static void pack_be(uint8_t *buf, size_t len)
{
	/* basic bandwidth efficient pack and unpack. see
	https://github.com/traud/asterisk-amr/blob/master/codecs/codec_amr.c */

	const int another = ((buf[1] >> 7) & 0x01);
	const int type    = ((buf[1] >> 3) & 0x0f);
	const int quality = ((buf[1] >> 2) & 0x01);
	unsigned int i;

	/* to shift in place, clear bits beyond end and at start */
	buf[0] = 0;
	buf[1] = 0;
	buf[len+1] = 0;
	/* shift in place, 6 bits */
	for (i = 1; i <= len; i++) {
		buf[i] = ((buf[i] << 6) | (buf[i + 1] >> 2));
	}
	/* restore first two bytes: [ CMR |F| FT |Q] */
	buf[1] |= ((type << 7) | (quality << 6));
	buf[0] = ((15 << 4) | (another << 3) | (type >> 1)); /* CMR: no */
}

static void unpack_be(uint8_t *temp, const uint8_t *buf, size_t len)
{
	const int another = ((buf[0] >> 3) & 0x01);
	const int type    = ((buf[0] << 1 | buf[1] >> 7) & 0x0f);
	const int quality = ((buf[1] >> 6) & 0x01);
	unsigned int i;

	/* shift in place, 2 bits */
	for (i = 1; i < (len - 1); i++) {
		temp[i] = ((buf[i] << 2) | (buf[i + 1] >> 6));
	}
	temp[len - 1] = buf[len - 1] << 2;
	/* restore first byte: [F| FT |Q] */
	temp[0] = ((another << 7) | (type << 3) | (quality << 2));
}

#ifdef AMR_WB
static int encode_wb(struct auenc_state *st,
		     bool *marker, uint8_t *buf, size_t *len,
		     int fmt, const void *sampv, size_t sampc)
{
	const struct amr_aucodec *amr_ac = (struct amr_aucodec *)st->ac;
	int n;
	(void)marker;

	if (sampc != L_FRAME16k)
		return EINVAL;

	if (*len < (1+NB_SERIAL_MAX))
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	n = IF2E_IF_encode(st->enc, 8, sampv, &buf[1], 0);
	if (n <= 0)
		return EPROTO;

	if (amr_ac->aligned) {
		/* CMR value 15 indicates that no mode request is present */
		buf[0] = 15 << 4;
		*len = (1 + n);
	}
	else {
		pack_be(buf, n);
		*len = n;
	}

	return 0;
}


static int decode_wb(struct audec_state *st,
		     int fmt, void *sampv, size_t *sampc,
		     bool marker, const uint8_t *buf, size_t len)
{
	const struct amr_aucodec *amr_ac = (struct amr_aucodec *)st->ac;
	(void)marker;

	if (*sampc < L_FRAME16k)
		return ENOMEM;
	if (len > (1+NB_SERIAL_MAX))
		return EINVAL;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (amr_ac->aligned) {
		IF2D_IF_decode(st->dec, &buf[1], sampv, 0);
	}
	else {
		unpack_be(amr_ac->be_dec_arr, buf, len);
		IF2D_IF_decode(st->dec, amr_ac->be_dec_arr, sampv, 0);
	}

	*sampc = L_FRAME16k;

	return 0;
}
#endif


#ifdef AMR_NB
static int encode_nb(struct auenc_state *st, bool *marker, uint8_t *buf,
		     size_t *len, int fmt, const void *sampv, size_t sampc)
{
	const struct amr_aucodec *amr_ac;
	int r;
	(void)marker;

	if (!st || !buf || !len || !sampv || sampc != FRAMESIZE_NB)
		return EINVAL;

	amr_ac = (struct amr_aucodec *)st->ac;

	if (*len < NB_SERIAL_MAX)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	r = Encoder_Interface_Encode(st->enc, MR122, sampv, &buf[1], 0);
	if (r <= 0)
		return EPROTO;

	if (amr_ac->aligned) {
		/* CMR value 15 indicates that no mode request is present */
		buf[0] = 15 << 4;
		*len = (1 + r);
	}
	else {
		pack_be(buf, r);
		*len = r;
	}

	return 0;
}


static int decode_nb(struct audec_state *st, int fmt, void *sampv,
		     size_t *sampc,
		     bool marker, const uint8_t *buf, size_t len)
{
	const struct amr_aucodec *amr_ac;
	(void)marker;

	if (!st || !sampv || !sampc || !buf)
		return EINVAL;

	amr_ac = (struct amr_aucodec *)st->ac;

	if (len > NB_SERIAL_MAX)
		return EPROTO;

	if (*sampc < L_FRAME16k)
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	if (amr_ac->aligned) {
		Decoder_Interface_Decode(st->dec, &buf[1], sampv, 0);
	}
	else {
		unpack_be(amr_ac->be_dec_arr, buf, len);
		Decoder_Interface_Decode(
			st->dec, amr_ac->be_dec_arr, sampv, 0);
	}

	*sampc = FRAMESIZE_NB;

	return 0;
}
#endif


#ifdef AMR_WB
static struct amr_aucodec amr_wb = {
	.ac = {
		.name      = "AMR-WB",
		.srate     = 16000,
		.crate     = 16000,
		.ch        = 1,
		.pch       = 1,
		.encupdh   = encode_update,
		.ench      = encode_wb,
		.decupdh   = decode_update,
		.dech      = decode_wb,
		.fmtp_ench = amr_fmtp_enc
	},
	.aligned = false,
	.be_dec_arr = NULL
};
#endif
#ifdef AMR_NB
static struct amr_aucodec amr_nb = {
	.ac = {
		.name      = "AMR",
		.srate     = 8000,
		.crate     = 8000,
		.ch        = 1,
		.pch       = 1,
		.encupdh   = encode_update,
		.ench      = encode_nb,
		.decupdh   = decode_update,
		.dech      = decode_nb,
		.fmtp_ench = amr_fmtp_enc
	},
	.aligned = false,
	.be_dec_arr = NULL
};
#endif


static int module_init(void)
{
	int err = 0;

#ifdef AMR_WB
	aucodec_register(baresip_aucodecl(), (struct aucodec *) &amr_wb);
#endif
#ifdef AMR_NB
	aucodec_register(baresip_aucodecl(), (struct aucodec *) &amr_nb);
#endif

	return err;
}


static int module_close(void)
{
#ifdef AMR_WB
	aucodec_unregister((struct aucodec *) &amr_wb);
#endif
#ifdef AMR_NB
	aucodec_unregister((struct aucodec *) &amr_nb);
#endif

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(amr) = {
	"amr",
	"codec",
	module_init,
	module_close
};
