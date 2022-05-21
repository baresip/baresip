/**
 * @file auconv.c  Audio sample format converter
 *
 * Copyright (C) 2021 Alfred E. Heggestad
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>


struct auconv_enc {
	struct aufilt_enc_st af;  /* base class */

	enum aufmt target_fmt;
	void *buf;
	size_t sampc;
};


struct auconv_dec {
	struct aufilt_dec_st af;  /* base class */

	enum aufmt target_fmt;
	void *buf;
	size_t sampc;
};


static void enc_destructor(void *arg)
{
	struct auconv_enc *st = arg;

	list_unlink(&st->af.le);
	mem_deref(st->buf);
}


static void dec_destructor(void *arg)
{
	struct auconv_dec *st = arg;

	list_unlink(&st->af.le);
	mem_deref(st->buf);
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct auconv_enc *st;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return EINVAL;

	st->target_fmt = conf_config()->audio.enc_fmt;

	*stp = (struct aufilt_enc_st *)st;

	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct auconv_dec *st;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return EINVAL;

	st->target_fmt = conf_config()->audio.play_fmt;

	*stp = (struct aufilt_dec_st *)st;

	return 0;
}


static int process_frame(void *buf, enum aufmt target_fmt,
			 struct auframe *af)
{
	switch (target_fmt) {

	case AUFMT_S16LE:
		auconv_to_s16(buf, af->fmt, af->sampv, af->sampc);
		break;

	case AUFMT_FLOAT:
		auconv_to_float(buf, af->fmt, af->sampv, af->sampc);
		break;

	default:
		warning("auconv: format not supported (%s)\n",
			aufmt_name(target_fmt));
		return ENOTSUP;
	}

	af->sampv = buf;
	af->fmt   = target_fmt;

	return 0;
}


static int encode_frame(struct aufilt_enc_st *st, struct auframe *af)
{
	struct auconv_enc *ac = (struct auconv_enc *)st;
	int err = 0;

	if (!st || !af)
		return EINVAL;

	if (af->fmt != ac->target_fmt) {

		if (!ac->buf || af->sampc != ac->sampc) {

			size_t sz = aufmt_sample_size(ac->target_fmt);

			ac->buf = mem_reallocarray(ac->buf, af->sampc,
						   sz, NULL);
			if (!ac->buf)
				return ENOMEM;

			ac->sampc = af->sampc;
		}

		err = process_frame(ac->buf, ac->target_fmt, af);
	}

	return err;
}


static int decode_frame(struct aufilt_dec_st *st, struct auframe *af)
{
	struct auconv_dec *ac = (struct auconv_dec *)st;
	int err = 0;

	if (!st || !af)
		return EINVAL;

	if (af->fmt != ac->target_fmt) {

		if (!ac->buf || af->sampc != ac->sampc) {

			size_t sz = aufmt_sample_size(ac->target_fmt);

			ac->buf = mem_reallocarray(ac->buf, af->sampc,
						   sz, NULL);
			if (!ac->buf)
				return ENOMEM;

			ac->sampc = af->sampc;
		}

		err = process_frame(ac->buf, ac->target_fmt, af);
	}

	return err;
}


static struct aufilt auconv = {
	.name    = "auconv",
	.encupdh = encode_update,
	.ench    = encode_frame,
	.decupdh = decode_update,
	.dech    = decode_frame
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &auconv);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&auconv);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(auconv) = {
	"auconv",
	"filter",
	module_init,
	module_close
};
