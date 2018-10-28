/**
 * @file speex_pp.c  Speex Pre-processor
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <stdlib.h>
#include <speex/speex_preprocess.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup speex_pp speex_pp
 *
 * Audio pre-processor from libspeexdsp
 */


struct preproc {
	struct aufilt_enc_st af;    /* base class */
	SpeexPreprocessState *state;
};


/** Speex configuration */
static struct {
	int denoise_enabled;
	int agc_enabled;
	int vad_enabled;
	int dereverb_enabled;
	spx_int32_t agc_level;
} pp_conf = {
	1,
	1,
	1,
	1,
	8000
};


static void speexpp_destructor(void *arg)
{
	struct preproc *st = arg;

	if (st->state)
		speex_preprocess_state_destroy(st->state);

	list_unlink(&st->af.le);
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct preproc *st;
	unsigned sampc;
	(void)ctx;
	(void)au;

	if (!stp || !af || !prm)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("speex_pp: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), speexpp_destructor);
	if (!st)
		return ENOMEM;

	sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->state = speex_preprocess_state_init(sampc, prm->srate);
	if (!st->state)
		goto error;

	speex_preprocess_ctl(st->state, SPEEX_PREPROCESS_SET_DENOISE,
			     &pp_conf.denoise_enabled);
	speex_preprocess_ctl(st->state, SPEEX_PREPROCESS_SET_AGC,
			     &pp_conf.agc_enabled);

#ifdef SPEEX_PREPROCESS_SET_AGC_TARGET
	if (pp_conf.agc_enabled) {
		speex_preprocess_ctl(st->state,
				     SPEEX_PREPROCESS_SET_AGC_TARGET,
				     &pp_conf.agc_level);
	}
#endif

	speex_preprocess_ctl(st->state, SPEEX_PREPROCESS_SET_VAD,
			     &pp_conf.vad_enabled);
	speex_preprocess_ctl(st->state, SPEEX_PREPROCESS_SET_DEREVERB,
			     &pp_conf.dereverb_enabled);

	info("speex_pp: Speex preprocessor loaded: srate = %uHz\n",
	     prm->srate);

	*stp = (struct aufilt_enc_st *)st;
	return 0;

 error:
	mem_deref(st);
	return ENOMEM;
}


static int encode(struct aufilt_enc_st *st, void *sampv, size_t *sampc)
{
	struct preproc *pp = (struct preproc *)st;

	if (!*sampc)
		return 0;

	speex_preprocess_run(pp->state, sampv);

	return 0;
}


static void config_parse(struct conf *conf)
{
	uint32_t v;

	if (0 == conf_get_u32(conf, "speex_agc_level", &v))
		pp_conf.agc_level = v;
}


static struct aufilt preproc = {
	LE_INIT, "speex_pp", encode_update, encode, NULL, NULL
};

static int module_init(void)
{
	config_parse(conf_cur());
	aufilt_register(baresip_aufiltl(), &preproc);
	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&preproc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(speex_pp) = {
	"speex_pp",
	"filter",
	module_init,
	module_close
};
