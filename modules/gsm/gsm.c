/**
 * @file gsm.c  GSM Audio Codec
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <gsm.h> /* please report if you have problems finding this file */
#include <re.h>
#include <rem_au.h>
#include <baresip.h>


/**
 * @defgroup gsm gsm
 *
 * The GSM audio codec
 */


enum {
	FRAME_SIZE = 160
};


struct auenc_state {
	gsm enc;
};

struct audec_state {
	gsm dec;
};


static void encode_destructor(void *arg)
{
	struct auenc_state *st = arg;

	gsm_destroy(st->enc);
}


static void decode_destructor(void *arg)
{
	struct audec_state *st = arg;

	gsm_destroy(st->dec);
}


static int encode_update(struct auenc_state **aesp, const struct aucodec *ac,
			 struct auenc_param *prm, const char *fmtp)
{
	struct auenc_state *st;
	int err = 0;
	(void)ac;
	(void)prm;
	(void)fmtp;

	if (!aesp)
		return EINVAL;
	if (*aesp)
		return 0;

	st = mem_zalloc(sizeof(*st), encode_destructor);
	if (!st)
		return ENOMEM;

	st->enc = gsm_create();
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
	(void)ac;
	(void)fmtp;

	if (!adsp)
		return EINVAL;
	if (*adsp)
		return 0;

	st = mem_zalloc(sizeof(*st), decode_destructor);
	if (!st)
		return ENOMEM;

	st->dec = gsm_create();
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


static int encode(struct auenc_state *st, uint8_t *buf, size_t *len,
		  int fmt, const void *sampv, size_t sampc)
{
	if (sampc != FRAME_SIZE)
		return EPROTO;
	if (*len < sizeof(gsm_frame))
		return ENOMEM;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	gsm_encode(st->enc, (gsm_signal *)sampv, buf);

	*len = sizeof(gsm_frame);

	return 0;
}


static int decode(struct audec_state *st, int fmt, void *sampv, size_t *sampc,
		  const uint8_t *buf, size_t len)
{
	int ret;

	if (*sampc < FRAME_SIZE)
		return ENOMEM;
	if (len < sizeof(gsm_frame))
		return EBADMSG;

	if (fmt != AUFMT_S16LE)
		return ENOTSUP;

	ret = gsm_decode(st->dec, (gsm_byte *)buf, (gsm_signal *)sampv);
	if (ret)
		return EPROTO;

	*sampc = 160;

	return 0;
}


static struct aucodec ac_gsm = {
	LE_INIT, "3", "GSM", 8000, 8000, 1, 1, NULL,
	encode_update, encode, decode_update, decode, NULL, NULL, NULL
};


static int module_init(void)
{
	debug("gsm: GSM v%u.%u.%u\n", GSM_MAJOR, GSM_MINOR, GSM_PATCHLEVEL);

	aucodec_register(baresip_aucodecl(), &ac_gsm);
	return 0;
}


static int module_close(void)
{
	aucodec_unregister(&ac_gsm);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(gsm) = {
	"gsm",
	"codec",
	module_init,
	module_close
};
