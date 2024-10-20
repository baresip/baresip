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

	if (prm->fmt != AUFMT_S16LE) {
		warning("augain: format not supported (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return EINVAL;

	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int encode_frame(struct aufilt_enc_st *st, struct auframe *af)
{
	unsigned int abs_sample, highest_abs_sample = 0;
	double encode_gain = gain;
	double highest_possible_gain;
	int16_t sample, gained_sample;

	if (!st || !af || !af->sampv || !af->sampc)
		return EINVAL;

	for (size_t i=0; i<af->sampc; i++) {
		abs_sample = abs(((int16_t *)(af->sampv))[i]);
		if (abs_sample > highest_abs_sample)
			highest_abs_sample = abs_sample;
	}

	highest_possible_gain = 32767.0 / highest_abs_sample;
	if (encode_gain > highest_possible_gain)
		encode_gain = highest_possible_gain;

	for (size_t i=0; i<af->sampc; i++) {
		sample = ((int16_t *)(af->sampv))[i];
		gained_sample = (int16_t)(sample * encode_gain);
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


static int cmd_augain(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = (struct cmd_arg *)arg;
	(void)pf;
	double new_gain = 0.0;

	if (str_isset(carg->prm))
		new_gain = strtod(carg->prm, NULL);

	if (new_gain <= 0.0) {
		warning("augain: invalid gain value %s\n", carg->prm);
		return EINVAL;
	}

	gain = new_gain;
	info("augain: new gain is %.2f\n", gain);

	return 0;

}

static const struct cmd cmdv[] = {
	{"augain", 0, CMD_PRM, "Set augain <gain>", cmd_augain},
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &augain);

	conf_get_float(conf_cur(), "augain", &gain);

	info("augain: gaining by at most %.2f\n", gain);

	return cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	aufilt_unregister(&augain);

	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(augain) = {
	"augain",
	"filter",
	module_init,
	module_close
};
