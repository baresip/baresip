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
	uint32_t srate;
	size_t frame_size;
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


static int init_state(struct preproc *st, size_t frame_size)
{
	st->state = speex_preprocess_state_init((int)frame_size, st->srate);
	if (!st->state)
		return ENOMEM;

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

	st->frame_size = frame_size;

	info("speex_pp: state inited (frame_size=%zu)\n", frame_size);

	return 0;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct preproc *st;
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

	st->srate = prm->srate;

	info("speex_pp: Speex preprocessor loaded: srate = %uHz\n",
	     prm->srate);

	*stp = (struct aufilt_enc_st *)st;
	return 0;
}


static int encode(struct aufilt_enc_st *st, struct auframe *af)
{
	struct preproc *pp = (struct preproc *)st;
	int err;

	if (!st || !af)
		return EINVAL;

	if (!af->sampc)
		return 0;

	if (pp->state && af->sampc != pp->frame_size) {

		speex_preprocess_state_destroy(pp->state);
		pp->state = NULL;
	}

	if (!pp->state) {
		err = init_state(pp, af->sampc);
		if (err)
			return err;
	}

	speex_preprocess_run(pp->state, af->sampv);

	return 0;
}


static void config_parse(struct conf *conf)
{
	uint32_t v;

	if (0 == conf_get_u32(conf, "speex_agc_level", &v))
		pp_conf.agc_level = v;
}


static struct aufilt preproc = {
	.name    = "speex_pp",
	.encupdh = encode_update,
	.ench    = encode,
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
