/**
 * @file auresamp.c  A filter module that inserts a resampler into the audio
 *                   pipeline if needed
 *
 * Copyright (C) 2022 Commend.com - c.spielberger@commend.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 *  The auresamp module is one of the audio filters. The order of the filters
 *  is specified by the order in the config file.
 *
 *  .    .--------.   .-------.   .----------.   .--------.
 *  |    |        |   |       |   | filters  |   |        |
 *  |O-->| ausrc  |-->| aubuf |-->|   e.g.   |-->| encode |--> RTP
 *  |    |        |   |       |   | auresamp |   |        |
 *  '    '---- ---'   '-------'   '----------'   '--------'
 *
 *       .--------.   .-------.   .----------.   .--------.
 * |\    |        |   |       |   | filters  |   |        |
 * | |<--| auplay |<--| aubuf |<--|   e.g.   |<--| decode |<-- RTP
 * |/    |        |   |       |   | auresamp |   |        |
 *       '--------'   '-------'   '----------'   '--------'
 */

struct auresamp_st {
	union {
		struct aufilt_enc_st eaf;
		struct aufilt_dec_st daf;
	} u;                     /* inheritance                              */

	int16_t *sampv;          /* s16le audio data buffer                  */
	int16_t *rsampv;         /* resampled data                           */
	size_t rsampsz;          /* size of rsampv buffer                    */
	struct auresamp resamp;  /* resampler                                */
	struct aufilt_prm oprm;  /* filter output parameters                 */
	const char *dbg;         /* debugging "encoder"/"decoder"            */
};


static void common_destructor(void *arg)
{
	struct auresamp_st *st = arg;

	mem_deref(st->rsampv);
	mem_deref(st->sampv);
}


static void enc_destructor(void *arg)
{
	struct auresamp_st *st = arg;

	list_unlink(&st->u.eaf.le);
	common_destructor(st);
}


static void dec_destructor(void *arg)
{
	struct auresamp_st *st = arg;

	list_unlink(&st->u.daf.le);
	common_destructor(st);
}


static int sampv_alloc(struct auresamp_st *st, struct auframe *af)
{
	size_t psize_out;
	size_t psize;

	/* s16le used internally */
	psize = af->sampc * af->ch * 2;

	/* output format == input format */
	psize_out = aufmt_sample_size(af->fmt) * af->sampc *
			st->oprm.srate * st->oprm.ch / (af->srate * af->ch);

	st->sampv = mem_zalloc(max(psize, psize_out), NULL);

	if (!st->sampv)
		return ENOMEM;

	return 0;
}


static int rsampv_check_size(struct auresamp_st *st, struct auframe *af)
{
	uint64_t ptime;
	size_t psize;

	ptime = af->sampc * 1000 / af->srate;
	psize = ptime * st->oprm.srate * st->oprm.ch *
		aufmt_sample_size(af->fmt) / 1000;

	/* auresamp minimum output size is the input size */
	psize = max(psize, auframe_size(af));
	if (st->rsampsz < psize) {
		st->rsampsz = 0;
		st->rsampv = mem_deref(st->rsampv);
		st->rsampv = mem_zalloc(psize, NULL);
	}

	if (!st->rsampv)
		return ENOMEM;

	st->rsampsz = psize;
	return 0;
}


static int resamp_setup(struct auresamp_st *st, struct auframe *af)
{
	int err = 0;

	err = auresamp_setup(&st->resamp, af->srate, af->ch,
			     st->oprm.srate, st->oprm.ch);
	if (err) {
		warning("resample: auresamp_setup error (%m)\n", err);
		return err;
	}

	return rsampv_check_size(st, af);
}


static int common_update(struct auresamp_st **stp, struct aufilt_prm *oprm,
			 mem_destroy_h *dh)
{
	struct auresamp_st *st;
	if (!stp || !oprm)
		return EINVAL;

	if (!oprm->ch || !oprm->srate)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), dh);
	if (!st)
		return ENOMEM;

	st->oprm = *oprm;
	auresamp_init(&st->resamp);

	*stp = st;
	return 0;
}


static int common_resample(struct auresamp_st *st, struct auframe *af)
{
	size_t rsampc;
	int16_t *sampv;
	int err = 0;

	if (st->dbg) {
		debug("auresamp: resample %s %u/%u --> %u/%u\n", st->dbg,
		      af->srate, af->ch, st->oprm.srate, st->oprm.ch);
		st->dbg = NULL;
	}

	if (!af->ch || !af->srate)
		return EINVAL;

	if (st->oprm.srate == af->srate && st->oprm.ch == af->ch) {
		st->rsampsz = 0;
		st->rsampv = mem_deref(st->rsampv);
		st->sampv  = mem_deref(st->sampv);
		return 0;
	}

	sampv  = af->sampv;
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
	else
		err = rsampv_check_size(st, af);

	if (err)
		return err;

	rsampc = st->rsampsz / 2;
	err = auresamp(&st->resamp, st->rsampv, &rsampc, sampv, af->sampc);
	if (err) {
		warning("resample: auresamp error (%m)\n", err);
		return err;
	}

	af->sampc = rsampc;
	af->fmt   = st->oprm.fmt;
	af->srate = st->oprm.srate;
	af->ch    = st->oprm.ch;
	if (st->oprm.fmt != AUFMT_S16LE) {
		auconv_from_s16(st->oprm.fmt, st->sampv, st->rsampv, rsampc);
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

	err = common_update(cstp, oprm, enc_destructor);
	if (err)
		return err;

	(*cstp)->dbg = "encoder";
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

	err = common_update(cstp, oprm, dec_destructor);
	if (err)
		return err;

	(*cstp)->dbg = "decoder";
	return 0;
}


static int encode(struct aufilt_enc_st *aufilt_enc_st, struct auframe *af)
{
	struct auresamp_st *st = (struct auresamp_st *) aufilt_enc_st;

	if (!st || !af)
		return EINVAL;

	return common_resample(st, af);
}


static int decode(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct auresamp_st *st = (struct auresamp_st *) aufilt_dec_st;

	if (!st || !af)
		return EINVAL;

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
