/**
 * @file plc.c  PLC -- Packet Loss Concealment
 *
 * Copyright (C) 2010 Creytiv.com
 */

#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES

#include <spandsp.h>
#include <re.h>
#include <rem_au.h>
#include <baresip.h>


/**
 * @defgroup plc plc
 *
 * Packet Loss Concealment (PLC) audio-filter using spandsp
 *
 */


struct plc_st {
	struct aufilt_dec_st af; /* base class */
	plc_state_t plc;
	size_t sampc;
};


static void destructor(void *arg)
{
	struct plc_st *st = arg;

	list_unlink(&st->af.le);
}


static int update(struct aufilt_dec_st **stp, void **ctx,
		  const struct aufilt *af, struct aufilt_prm *prm,
		  const struct audio *au)
{
	struct plc_st *st;
	int err = 0;
	(void)ctx;
	(void)af;
	(void)au;

	if (!stp || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	if (prm->ch != 1) {
		warning("plc: only mono supported (ch=%u)\n", prm->ch);
		return ENOSYS;
	}

	if (prm->fmt != AUFMT_S16LE) {
		warning("plc: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	if (!plc_init(&st->plc)) {
		err = ENOMEM;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = (struct aufilt_dec_st *)st;

	return err;
}


/*
 * PLC is only valid for Decoding (RX)
 *
 * NOTE: sampc == 0 , means Packet loss
 */
static int decode(struct aufilt_dec_st *st, struct auframe *af)
{
	struct plc_st *plc = (struct plc_st *)st;

	if (!st || !af)
		return EINVAL;

	if (af->sampc) {
		plc_rx(&plc->plc, af->sampv, (int)af->sampc);
		plc->sampc = af->sampc;
	}
	else if (plc->sampc)
		af->sampc = plc_fillin(&plc->plc, af->sampv, (int)plc->sampc);

	return 0;
}


static struct aufilt plc = {
	.name    = "plc",
	.decupdh = update,
	.dech    = decode
};


static int module_init(void)
{
	aufilt_register(baresip_aufiltl(), &plc);

	return 0;
}


static int module_close(void)
{
	aufilt_unregister(&plc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(plc) = {
	"plc",
	"filter",
	module_init,
	module_close
};
