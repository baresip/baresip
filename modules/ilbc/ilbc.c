/**
 * @file ilbc.c  Internet Low Bit Rate Codec (iLBC) audio codec
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include <iLBC_define.h>
#include <iLBC_decode.h>
#include <iLBC_encode.h>


/**
 * @defgroup ilbc ilbc
 *
 * iLBC audio codec
 *
 * This module implements the iLBC audio codec as defined in:
 *
 *     RFC 3951  Internet Low Bit Rate Codec (iLBC)
 *     RFC 3952  RTP Payload Format for iLBC Speech
 *
 * The iLBC source code is not included here, but can be downloaded from
 * http://ilbcfreeware.org/
 *
 * You can also use the source distributed by the Freeswitch project,
 * see www.freeswitch.org, and then freeswitch/libs/codec/ilbc.
 * Or you can look in the asterisk source code ...
 *
 *   mode=20  15.20 kbit/s  160samp  38bytes
 *   mode=30  13.33 kbit/s  240samp  50bytes
 */

enum {
	DEFAULT_MODE = 20, /* 20ms or 30ms */
	USE_ENHANCER = 1
};

struct auenc_state {
	iLBC_Enc_Inst_t enc;
	int mode;
	uint32_t enc_bytes;
};

struct audec_state {
	iLBC_Dec_Inst_t dec;
	int mode;
	uint32_t nsamp;
	size_t dec_bytes;
};


static char ilbc_fmtp[32];


static void set_encoder_mode(struct auenc_state *st, int mode)
{
	if (st->mode == mode)
		return;

	info("ilbc: set iLBC encoder mode %dms\n", mode);

	st->mode = mode;

	switch (mode) {

	case 20:
		st->enc_bytes = NO_OF_BYTES_20MS;
		break;

	case 30:
		st->enc_bytes = NO_OF_BYTES_30MS;
		break;

	default:
		warning("ilbc: unknown encoder mode %d\n", mode);
		return;
	}

	st->enc_bytes = initEncode(&st->enc, mode);
}


static void set_decoder_mode(struct audec_state *st, int mode)
{
	if (st->mode == mode)
		return;

	info("ilbc: set iLBC decoder mode %dms\n", mode);

	st->mode = mode;

	switch (mode) {

	case 20:
		st->nsamp = BLOCKL_20MS;
		break;

	case 30:
		st->nsamp = BLOCKL_30MS;
		break;

	default:
		warning("ilbc: unknown decoder mode %d\n", mode);
		return;
	}

	st->nsamp = initDecode(&st->dec, mode, USE_ENHANCER);
}


static void encoder_fmtp_decode(struct auenc_state *st, const char *fmtp)
{
	struct pl mode;

	if (!fmtp)
		return;

	if (re_regex(fmtp, strlen(fmtp), "mode=[0-9]+", &mode))
		return;

	set_encoder_mode(st, pl_u32(&mode));
}


static void decoder_fmtp_decode(struct audec_state *st, const char *fmtp)
{
	struct pl mode;

	if (!fmtp)
		return;

	if (re_regex(fmtp, strlen(fmtp), "mode=[0-9]+", &mode))
		return;

	set_decoder_mode(st, pl_u32(&mode));
}


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;
	(void)st;
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;
	(void)st;
}


static int check_ptime(const struct auenc_param *prm)
{
	if (!prm)
		return 0;

	switch (prm->ptime) {

	case 20:
	case 30:
		return 0;

	default:
		warning("ilbc: invalid ptime %u ms\n", prm->ptime);
		return EINVAL;
	}
}


static int encode_update(struct auenc_state **aesp, const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	int err = 0;

	if (!aesp || !ac || !prm)
		return EINVAL;
	if (check_ptime(prm))
		return EINVAL;
	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	set_encoder_mode(st, DEFAULT_MODE);

	if (str_isset(fmtp))
		encoder_fmtp_decode(st, fmtp);

	/* update parameters after SDP was decoded */
	if (prm) {
		prm->ptime = st->mode;
	}

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

	set_decoder_mode(st, DEFAULT_MODE);

	if (str_isset(fmtp))
		decoder_fmtp_decode(st, fmtp);

	if (err)
		mem_deref(st);
	else
		*adsp = st;

	return err;
}


static int encode(struct auenc_state *st, uint8_t *buf,
		  size_t *len, const int16_t *sampv, size_t sampc)
{
	float float_buf[sampc];
	uint32_t i;

	/* Make sure there is enough space */
	if (*len < st->enc_bytes) {
		warning("ilbc: encode: buffer is too small (%u bytes)\n",
			*len);
		return ENOMEM;
	}

	/* Convert from 16-bit samples to float */
	for (i=0; i<sampc; i++) {
		const int16_t v = sampv[i];
		float_buf[i] = (float)v;
	}

	iLBC_encode(buf,            /* (o) encoded data bits iLBC */
		    float_buf,      /* (o) speech vector to encode */
		    &st->enc);      /* (i/o) the general encoder state */

	*len = st->enc_bytes;

	return 0;
}


static int do_dec(struct audec_state *st, int16_t *sampv, size_t *sampc,
		  const uint8_t *buf, size_t len)
{
	float float_buf[st->nsamp];
	const int mode = len ? 1 : 0;
	uint32_t i;

	/* Make sure there is enough space in the buffer */
	if (*sampc < st->nsamp)
		return ENOMEM;

	iLBC_decode(float_buf,      /* (o) decoded signal block */
		    (uint8_t *)buf, /* (i) encoded signal bits */
		    &st->dec,       /* (i/o) the decoder state structure */
		    mode);          /* (i) 0: bad packet, PLC, 1: normal */

	/* Convert from float to 16-bit samples */
	for (i=0; i<st->nsamp; i++) {
		sampv[i] = (int16_t)float_buf[i];
	}

	*sampc = st->nsamp;

	return 0;
}


static int decode(struct audec_state *st, int16_t *sampv,
		  size_t *sampc, const uint8_t *buf, size_t len)
{
	/* Try to detect mode */
	if (st->dec_bytes != len) {

		st->dec_bytes = len;

		switch (st->dec_bytes) {

		case NO_OF_BYTES_20MS:
			set_decoder_mode(st, 20);
			break;

		case NO_OF_BYTES_30MS:
			set_decoder_mode(st, 30);
			break;

		default:
			warning("ilbc: decode: expect %u, got %u\n",
				st->dec_bytes, len);
			return EINVAL;
		}
	}

	return do_dec(st, sampv, sampc, buf, len);
}


static int pkloss(struct audec_state *st, int16_t *sampv, size_t *sampc)
{
	return do_dec(st, sampv, sampc, NULL, 0);
}


static struct aucodec ilbc = {
	LE_INIT, 0, "iLBC", 8000, 8000, 1, 1, ilbc_fmtp,
	encode_update, encode, decode_update, decode, pkloss, 0, 0
};


static int module_init(void)
{
	(void)re_snprintf(ilbc_fmtp, sizeof(ilbc_fmtp),
			  "mode=%d", DEFAULT_MODE);

	aucodec_register(baresip_aucodecl(), &ilbc);
	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&ilbc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(ilbc) = {
	"ilbc",
	"codec",
	module_init,
	module_close
};
