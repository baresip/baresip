/**
 * @file augain.c  Audio gain
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * Audio gain module
 *
 * This module can be used to increase volume of audio source,
 * for example, microphone.
 *
 * Sample config:
 *
 \verbatim
  augain            1.5
 \endverbatim
 */


struct augain_enc {
	struct aufilt_enc_st af;  /* base class */
};


static double gain = 1.0;


static int conf_get_float(const struct conf *conf, const char *name,
			  double *val)
{
	struct pl opt;
	int err;

	if (!conf || !name || !val)
		return EINVAL;

	err = conf_get(conf, name, &opt);
	if (err)
		return err;

	*val = pl_float(&opt);

	return 0;
}


static void enc_destructor(void *arg)
{
	struct augain_enc *st = arg;

	list_unlink(&st->af.le);
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct augain_enc *st;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return EINVAL;

	conf_get_float(conf_cur(), "augain", &gain);

	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int encode_frame(struct aufilt_enc_st *st, struct auframe *af)
{
	size_t i;
	unsigned int abs_sample, highest_abs_sample = 0;
	double encode_gain = gain;
	double highest_possible_gain;
	int16_t sample, gained_sample;

	if (!st || !af || !af->sampv || !af->sampc)
		return EINVAL;

	if (af->fmt != AUFMT_S16LE) {
		warning("augain: format not supported (%s)\n",
			aufmt_name(af->fmt));
		return ENOTSUP;
	}

	for (i=0; i<af->sampc; i++) {
		abs_sample = abs(((int16_t *)(af->sampv))[i]);
		if (abs_sample > highest_abs_sample)
			highest_abs_sample = abs_sample;
	}

	highest_possible_gain = 32767.0 / highest_abs_sample;
	if (encode_gain > highest_possible_gain)
		encode_gain = highest_possible_gain;

	/*
	info("augain: sample count=%d/highest abs=%d/encode_gain=%.2f\n",
	     af->sampc, highest_abs_sample, encode_gain);
	*/

	for (i=0; i<af->sampc; i++) {
		sample = ((int16_t *)(af->sampv))[i];
		gained_sample = (int16_t)(sample * encode_gain);
		/*
		info("augain: sample=%d/gained=%d\n", sample, gained_sample);
		*/
		((int16_t *)(af->sampv))[i] = gained_sample;
	}

	return 0;
}


static struct aufilt augain = {
	.name    = "augain",
	.encupdh = encode_update,
	.ench    = encode_frame,
	.decupdh = NULL,
	.dech    = NULL
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &augain);

	conf_get_float(conf_cur(), "augain", &gain);

	info("augain: gaining by %.2f\n", gain);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&augain);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(augain) = {
	"augain",
	"filter",
	module_init,
	module_close
};
