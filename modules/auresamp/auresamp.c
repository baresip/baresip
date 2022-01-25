/**
 * @file auresamp.c  A filter module that inserts a resampler into the audio
 *                   pipeline if needed
 *
 * Copyright (C) 2022 Commend.com - c.spielberger@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>

enum {
	MAX_PTIME       =    60,  /* Maximum packet time in [ms] */
};

/**
 *  The auresamp module is one of the audio filters. The order of the filters
 *  is specified by the order in the config file.
 *
 *  .    .-------.   .----------.   .-------.   .---------.
 *  |    |       |   |          |   |       |   |         |
 *  |O-->| ausrc |-->| auresamp |-->| aubuf |-->| encoder |--> RTP
 *  |    |       |   |          |   |       |   |         |
 *  '    '-------'   '----------'   '-------'   '---------'
 *
 *       .--------.   .-------.   .----------.   .--------.
 * |\    |        |   |       |   |          |   |        |
 * | |<--| auplay |<--| aubuf |<--| auresamp |<--| decode |<-- RTP
 * |/    |        |   |       |   |          |   |        |
 *       '--------'   '-------'   '----------'   '--------'
 */

struct auresamp_st {
	union {
		struct aufilt_enc_st eaf;
		struct aufilt_dec_st daf;
	};                       /* inheritance              */

	int16_t *sampv;          /* s16le audio data buffer  */
	int16_t *rsampv;         /* resampled data           */
	struct auresamp resamp;  /* resampler                */
	struct aufilt_prm oprm;  /* filter output parameters */
};


static void destructor(void *arg)
{
	struct auresamp_st *st = arg;

	mem_deref(st->rsampv);
	mem_deref(st->sampv);
}


static int sampv_alloc(struct auresamp_st *st, struct auframe *af)
{
	size_t psize;

	psize = MAX_PTIME * af->srate * af->ch * sizeof(int16_t) / 1000;
	st->sampv = mem_zalloc(psize, NULL);

	if (!st->sampv)
		return ENOMEM;

	return 0;
}


static int resamp_setup(struct auresamp_st *st, struct auframe *af)
{
	int err = 0;
	size_t psize;

	err = auresamp_setup(&st->resamp, af->srate, af->ch,
			     st->oprm.srate, st->oprm.ch);
	if (err) {
		warning("resample: auresamp_setup error (%m)\n", err);
		return err;
	}

	psize = MAX_PTIME * st->oprm.srate * st->oprm.ch * 2 / 1000;

	st->rsampv = mem_deref(st->rsampv);
	st->rsampv = mem_zalloc(psize, NULL);
	if (!st->rsampv)
		return ENOMEM;

	return 0;
}


static int common_update(struct auresamp_st **stp, struct aufilt_prm *oprm)
{
	struct auresamp_st *st;
	if (!stp || !oprm)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->oprm = *oprm;
	auresamp_init(&st->resamp);

	*stp = st;
	return 0;
}


static int common_resample(struct auresamp_st *st, struct auframe *af)
{
	size_t outc;
	int16_t *sampv = af->sampv;
	int err = 0;

	if (st->oprm.srate == af->srate && st->oprm.ch == af->ch)
		return 0;

	if (af->fmt != AUFMT_S16LE) {
		if (!st->sampv)
			err = sampv_alloc(st, af);

		if (err)
			return err;

		auconv_to_s16(st->sampv, af->fmt, af->sampv, af->sampc);
		sampv = st->sampv;
	}

	if (st->resamp.irate != af->srate || st->resamp.ich != af->ch)
		err = resamp_setup(st, af);

	if (err)
		return err;

	err = auresamp(&st->resamp, st->rsampv, &outc, sampv, af->sampc);
	if (err) {
		warning("resample: auresamp error (%m)\n", err);
		return err;
	}

	af->sampc = outc;
	af->fmt = st->oprm.fmt;
	if (st->oprm.fmt != AUFMT_S16LE) {
		auconv_from_s16(st->oprm.fmt, st->sampv, st->rsampv, outc);
		af->sampv = st->sampv;
	}
	else {
		af->sampv = st->rsampv;
	}

	return err;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *oprm,
			 const struct audio *au)
{
	struct auresamp_st **cstp = (struct auresamp_st **) stp;
	int err;
	(void)af;
	(void)ctx;
	(void)au;

	err = common_update(cstp, oprm);
	if (err)
		return err;

	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *oprm,
			 const struct audio *au)
{
	struct auresamp_st **cstp = (struct auresamp_st **) stp;
	int err;
	(void)af;
	(void)ctx;
	(void)au;

	err = common_update(cstp, oprm);
	if (err)
		return err;

	return 0;
}


static int encode(struct aufilt_enc_st *aufilt_enc_st, struct auframe *af)
{
	struct auresamp_st *st = (struct auresamp_st *) aufilt_enc_st;
	return common_resample(st, af);
}


static int decode(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct auresamp_st *st = (struct auresamp_st *) aufilt_dec_st;
	return common_resample(st, af);
}


static struct aufilt resample = {
	LE_INIT, "auresamp", encode_update, encode, decode_update, decode
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &resample);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&resample);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(auresamp) = {
	"auresamp",
	"filter",
	module_init,
	module_close
};
