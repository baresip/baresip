/**
 * @file dtmf.c  DTMF decoder
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <time.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup dtmf dtmf
 *
 * Audio filter that decodes DTMF tones
 *
 */


static void dtmf_dec_handler(char digit, void *arg)
{
	char key_str[2];

	key_str[0] = digit;
	key_str[1] = '\0';

	ua_event(NULL, UA_EVENT_CALL_INBAND_DTMF, NULL, key_str);
}

struct dtmf_filt_dec {
	struct aufilt_dec_st af;  /* base class */
	struct dtmf_dec *dec;
};


static void dec_destructor(void *arg)
{
	struct dtmf_filt_dec *st = arg;

	list_unlink(&st->af.le);
	mem_deref(st->dtmf_dec);
}



static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct dtmf_filt_dec *st;
	int err = 0;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return EINVAL;

	err = dtmf_dec_alloc(&st->dec, 8000, 1, dtmf_dec_handler, st->dbuf);

	if (err)
		mem_deref(st);
	else
	{
		dtmf_dec_reset(st->dec, 8000, 1);
		*stp = (struct aufilt_dec_st *)st;
	}

	return err;
}


static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct dtmf_filt_dec *sf = (struct dtmf_filt_dec *)st;

	if (!st || !af)
		return EINVAL;

	dtmf_dec_probe(sf->dec, af->sampv, af->sampc);

	return 0;
}


static struct aufilt dtmf = {
	.name    = "dtmf",
	.encupdh = NULL,
	.ench    = NULL,
	.decupdh = decode_update,
	.dech    = decode
};

static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &dtmf);

	info("dtmf: adding DTMF filter\n");

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&dtmf);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(dtmf) = {
	"dtmf",
	"filter",
	module_init,
	module_close
};
