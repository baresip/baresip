/**
 * @file mixausrc.c  Mixes another audio source into audio stream.
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup mixausrc mixausrc
 *
 * A command starts reading from specified audio source and mixes the audio
 * data into the current stream. For both, the original audio stream and
 * for the audio source a volume level between 0 and 100% can be given.
 *
 * When the alternative source gets an EOS (e.g. audio file) then the damping
 * of the stream is turned off.
 *
 * The switching is done by applying a fade in/out function to the original
 * stream. The audio source is not faded.
 */

#define DEFAULT_FADE_TIME 160  /*default fading time in ms*/

/**
 * State machine. See:
 * xdot mixausrc.dot
 *
 * start: FM_IDLE -> FM_FADEOUT
 * FM_FADEOUT     -> FM_MIX
 * FM_MIX         -> FM_FADEIN
 * FM_FADEIN      -> FM_IDLE
 *
 * restart:
 *		FM_FADEIN  -> FM_FADEOUT
 *		FM_FADEOUT -> FM_FADEOUT
 */
enum mixmode {
	FM_IDLE,
	FM_FADEOUT,
	FM_MIX,
	FM_FADEIN,
	FM_NONE
};


static const char *str_mixmode(enum mixmode m)
{
	switch (m) {
	case FM_IDLE:
		return "IDLE";

	case FM_FADEOUT:
		return "FADEOUT";

	case FM_MIX:
		return "MIX";

	case FM_FADEIN:
		return "FADEIN";

	case FM_NONE:
		return "NONE";

	default:
		return "?";
	}
}


struct mixstatus {
	struct ausrc_st *ausrc;         /**< Audio source                    */
	struct ausrc_prm ausrc_prm;     /**< Audio source parameter          */

	char *module;                   /**< Audio source module name        */
	char *param;                    /**< Parameter for audio source      */
	enum mixmode mode;              /**< Current mix mode                */
	enum mixmode nextmode;          /**< Next mix mode                   */
	float minvol;                   /**< Minimum audio stream volume     */
	float ausvol;                   /**< Volume for mixed audio source   */
	uint32_t ptime;                 /**< Stream packet time              */
	size_t sampc_strm;              /**< Stream sample count frame       */
	size_t nbytes_strm;             /**< Stream bytes per frame          */
	size_t sampc_max;               /**< Max sample count per frame      */
	size_t nbytes_max;              /**< Max bytes per frame             */
	uint16_t i_fade;                /**< Fade-in/-out counter            */
	uint16_t n_fade;                /**< Fade-in/-out steps              */
	float delta_fade;               /**< linear delta accumulation       */
	struct aufilt_prm prm;          /**< Audio filter parameter          */

	struct auresamp resamp;         /**< Optional audio resampler        */
	void *sampvrs;                  /**< Optional resample buffer        */

	struct aubuf *aubuf;
	bool aubuf_started;
	int16_t *rbuf;
};


struct mixausrc_enc {
	struct aufilt_enc_st af;  /* inheritance */

	struct mixstatus st;
	struct le le_priv;
};


struct mixausrc_dec {
	struct aufilt_dec_st af;  /* inheritance */

	struct mixstatus st;
	struct le le_priv;
};


static struct list encs;
static struct list decs;


static int stop_ausrc(struct mixstatus *st);


static int init_aubuf(struct mixstatus *st)
{
	int err;
	uint32_t v = 2;
	size_t maxsz;
	size_t wishsz;

	conf_get_u32(conf_cur(), "mixausrc_wish_size", &v);
	wishsz = v * st->nbytes_strm;
	maxsz = 2 * wishsz;

	st->aubuf = mem_deref(st->aubuf);
	st->aubuf_started = false;

	err = aubuf_alloc(&st->aubuf, wishsz, maxsz);
	if (err) {
		warning("mixausrc: Could not allocate aubuf. wishsz=%lu, "
				"maxsz=%lu (%m)\n", wishsz, maxsz, err);
		goto out;
	}

	aubuf_set_live(st->aubuf, false);
	if (st->rbuf)
		return 0;

	st->rbuf = mem_zalloc(st->nbytes_strm, NULL);
	if (!st->rbuf) {
		warning("mixausrc: Could not allocate rbuf.\n");
		goto out;
	}

out:
	return err;
}


static int process_resamp(struct mixstatus *st, const struct auframe *afsrc)
{
	int err = 0;
	if (afsrc->fmt != AUFMT_S16LE) {
		warning("mixausrc: sample format %s not supported\n",
			aufmt_name(afsrc->fmt));
		return EINVAL;
	}

	if (!st->resamp.resample) {
		debug("mixausrc: resample ausrc %u/%u -> %u/%u\n",
				st->ausrc_prm.srate, st->ausrc_prm.ch,
				st->prm.srate, st->prm.ch);
		err = auresamp_setup(&st->resamp,
				st->ausrc_prm.srate, st->ausrc_prm.ch,
				st->prm.srate, st->prm.ch);
		if (err) {
			warning("mixausrc: could not initialize a "
					"resampler (%m)\n", err);
			return err;
		}

		st->sampvrs = mem_deref(st->sampvrs);
		st->sampvrs = mem_zalloc(st->nbytes_max, NULL);
		if (!st->sampvrs) {
			warning("mixausrc: could not alloc resample buffer\n");
			return ENOMEM;
		}
	}

	if (st->resamp.resample) {
		size_t sampc = st->sampc_max;
		err = auresamp(&st->resamp, st->sampvrs, &sampc,
			       afsrc->sampv, afsrc->sampc);

		if (sampc != st->sampc_strm) {
			warning("mixausrc: unexpected sample count "
					"%u vs. %u\n", sampc, st->sampc_strm);
			st->sampc_strm = sampc;
			st->nbytes_strm = aufmt_sample_size(afsrc->fmt)*sampc;
		}
	}

	if (err)
		warning("mixausrc: could not resample frame (%m)\n", err);

	return err;
}


static void ausrc_prm_af(struct ausrc_prm *ausprm, struct auframe *afsrc)
{
	ausprm->srate = afsrc->srate;
	ausprm->ch    = afsrc->ch;
	ausprm->fmt   = afsrc->fmt;
}


static void ausrc_prm_aufilt(struct ausrc_prm *ausprm,
			     struct aufilt_prm *filprm)
{
	ausprm->srate = filprm->srate;
	ausprm->ch    = filprm->ch;
	ausprm->fmt   = filprm->fmt;
}


static void ausrc_read_handler(struct auframe *afsrc, void *arg)
{
	struct mixstatus *st = arg;
	int err = 0;

	if (!st->prm.srate || !st->prm.ch)
		return;

	ausrc_prm_af(&st->ausrc_prm, afsrc);
	if (!st->ausrc_prm.srate || !st->ausrc_prm.ch)
		return;

	if (!st->sampc_max || !st->nbytes_max)
		return;

	if (st->ausrc_prm.srate != st->prm.srate ||
			st->ausrc_prm.ch != st->prm.ch)
		err = process_resamp(st, afsrc);

	if (err) {
		st->nextmode = FM_FADEIN;
		return;
	}

	if (!st->aubuf) {
		err = init_aubuf(st);
		if (err) {
			st->nextmode = FM_FADEIN;
			return;
		}
	}

	if (st->sampvrs) {
		aubuf_write(st->aubuf, st->sampvrs, st->nbytes_strm);
	}
	else
		aubuf_write(st->aubuf, afsrc->sampv, st->nbytes_strm);
}


static void ausrc_error_handler(int err, const char *str, void *arg)
{
	struct mixstatus *st = arg;
	(void) err;
	(void) str;

	stop_ausrc(st);
	st->mode = FM_FADEIN;
}


static int start_ausrc(struct mixstatus *st)
{
	int err;

	auresamp_init(&st->resamp);
	st->ausrc_prm.ptime = st->ptime;
	err = ausrc_alloc(&st->ausrc, baresip_ausrcl(), st->module,
			  &st->ausrc_prm, st->param, ausrc_read_handler,
			  ausrc_error_handler, st);

	if (!st->ausrc) {
		warning("mixausrc: Could not start audio source %s with "
				"data %s.\n", st->module, st->param);
		err = EINVAL;
		st->mode = FM_FADEIN;
	}

	/* now we are ready for next start_process */
	st->module = mem_deref(st->module);
	st->param = mem_deref(st->param);
	st->sampc_max = st->ausrc_prm.srate * st->ausrc_prm.ptime *
			st->ausrc_prm.ch / 1000;
	if (st->sampc_strm > st->sampc_max)
		st->sampc_max = st->sampc_strm;

	st->nbytes_max = st->sampc_max * aufmt_sample_size(st->ausrc_prm.fmt);

	return err;
}


static int stop_ausrc(struct mixstatus *st)
{
	st->ausrc   = mem_deref(st->ausrc);
	st->aubuf   = mem_deref(st->aubuf);
	st->rbuf    = mem_deref(st->rbuf);
	st->sampvrs = mem_deref(st->sampvrs);
	st->sampc_max  = 0;
	st->nbytes_max = 0;
	st->aubuf_started = false;
	return 0;
}


static void destruct(struct mixstatus *st)
{
	stop_ausrc(st);
	mem_deref(st->module);
	mem_deref(st->param);
}


static void enc_destructor(void *arg)
{
	struct mixausrc_enc *st = (struct mixausrc_enc *) arg;

	list_unlink(&st->le_priv);
	destruct(&st->st);
}


static void dec_destructor(void *arg)
{
	struct mixausrc_dec *st = (struct mixausrc_dec *)arg;

	list_unlink(&st->le_priv);
	destruct(&st->st);
}


static void mixstatus_init(struct mixstatus *st, struct aufilt_prm *prm)
{

	stop_ausrc(st);

	st->mode = FM_IDLE;
	st->minvol = 1.;
	st->ausvol = 1.;
	st->i_fade = 0;

	/* initialize with configured values */
	st->prm = *prm;
	st->ausrc_prm.ch = prm->ch;
	st->ausrc_prm.fmt = prm->fmt;
	st->ausrc_prm.srate = prm->srate;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct mixausrc_enc *st;
	(void)au;
	(void)af;
	(void)prm;

	if (!stp || !ctx || !prm)
		return EINVAL;

	if (prm->ch!=1) {
		warning("mixausrc: Only mono is supported.\n");
		return EINVAL;
	}

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), enc_destructor);
	if (!st)
		return ENOMEM;

	list_append(&encs, &st->le_priv, st);
	*stp = (struct aufilt_enc_st *) st;

	mixstatus_init(&st->st, prm);
	return 0;
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct mixausrc_dec *st;
	(void)au;
	(void)af;

	if (!stp || !ctx)
		return EINVAL;

	if (*stp)
		return 0;

	st = mem_zalloc(sizeof(*st), dec_destructor);
	if (!st)
		return ENOMEM;

	list_append(&decs, &st->le_priv, st);
	*stp = (struct aufilt_dec_st *) st;

	mixstatus_init(&st->st, prm);
	return 0;
}


/**
 * Fade-In:  values from st->minvol to 1.0
 * Fade-Out: values form 1.0 to st->minvol
 *
 * @param st   mixausrc status object
 * @param dir  fading direction (fade-in / fade-out)
 *
 * @return float sample multiplication factor
 */
static float fade_linear(struct mixstatus *st, enum mixmode dir)
{
	float factor = st->i_fade * st->delta_fade;
	++st->i_fade;

	if (dir == FM_FADEIN)
		return (st->minvol + factor) > 1. ? 1. : st->minvol + factor;
	else
		return (1. - factor) < st->minvol ? st->minvol : 1. - factor;
}


static void fade_int16(struct mixstatus *st, int16_t *data, uint16_t n,
	enum mixmode dir)
{
	uint16_t i;
	for (i = 0; (i < n) && (st->i_fade < st->n_fade); ++i) {
		data[i] *= fade_linear(st, dir);
	}
}


static void fade_float(struct mixstatus *st, float *data, uint16_t n,
	enum mixmode dir)
{
	uint16_t i;
	for (i = 0; (i < n) && (st->i_fade < st->n_fade); ++i)
		data[i] *= fade_linear(st, dir);
}


static int fadeframe(struct mixstatus *st, struct auframe *af,
	enum mixmode dir)
{
	if (af->fmt == AUFMT_S16LE)
		fade_int16(st, af->sampv, af->sampc, dir);
	else if (af->fmt == AUFMT_FLOAT)
		fade_float(st, af->sampv, af->sampc, dir);
	else
		return EINVAL;

	return 0;
}


static void clear_int16(struct mixstatus *st, int16_t *data, uint16_t n)
{
	uint16_t i;
	for (i = 0; i < n; ++i)
		data[i] *= st->minvol;
}


static void clear_float(struct mixstatus *st, float *data, uint16_t n)
{
	uint16_t i;
	for (i = 0; i < n; ++i)
		data[i] *= st->minvol;
}


static int clear_frame(struct mixstatus *st, struct auframe *af)
{
	if (af->fmt == AUFMT_S16LE)
		clear_int16(st, af->sampv, af->sampc);
	else if (af->fmt == AUFMT_FLOAT)
		clear_float(st, af->sampv, af->sampc);
	else
		return EINVAL;

	return 0;
}


static void mix_int16(struct mixstatus *st, int16_t *data, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		data[i] = (int16_t) (data[i] * st->minvol +
				(st->ausvol * st->rbuf[i]));
}


static void mix_float(struct mixstatus *st, float *data, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		data[i] = data[i] * st->minvol +
			(st->ausvol * st->rbuf[i]);
}


static int mixframe(struct mixstatus *st, struct auframe *af)
{
	if (af->fmt == AUFMT_S16LE)
		mix_int16(st, af->sampv, af->sampc);
	else if (af->fmt == AUFMT_FLOAT)
		mix_float(st, af->sampv, af->sampc);
	else
		return EINVAL;

	return 0;
}


static void aufilt_prm_update(struct aufilt_prm *prm, struct auframe *af)
{
	prm->srate = af->srate;
	prm->ch    = af->ch;
	prm->fmt   = af->fmt;
}


static int process(struct mixstatus *st, struct auframe *af)
{
	size_t n = af->sampc;
	uint32_t ptime = (uint32_t) n * 1000 / (af->srate * af->ch);
	int err = 0;

	st->sampc_strm = af->sampc;
	st->nbytes_strm = auframe_size(af);
	if (!st->ptime) {
		st->ptime = ptime;
	}
	else if (st->ptime != ptime) {
		warning("mixausrc: ptime changed %u --> %u.\n",
			st->ptime, ptime);
		stop_ausrc(st);
		st->nextmode = FM_FADEIN;
		return EINVAL;
	}

	aufilt_prm_update(&st->prm, af);
	if (st->mode == FM_FADEOUT && st->i_fade == st->n_fade)
		st->mode = FM_MIX;

	/* process nextmode */
	if (st->mode == FM_MIX && st->nextmode == FM_FADEOUT)
		st->nextmode = FM_NONE;

	else if (st->mode == FM_IDLE && st->nextmode == FM_FADEIN)
		st->nextmode = FM_NONE;

	else if (st->nextmode != FM_NONE) {
		/* a command was invoked */
		/* process nextmode */
		if (st->mode != st->nextmode) {
			debug("mixausrc: mode %s --> %s\n",
					str_mixmode(st->mode),
					str_mixmode(st->nextmode));
			if (st->mode == FM_MIX)
				stop_ausrc(st);
		}

		st->mode = st->nextmode;
		st->nextmode = FM_NONE;
	}

	switch (st->mode) {
	case FM_FADEIN: {
		err = fadeframe(st, af, st->mode);
		if (st->i_fade >= st->n_fade) {
			st->i_fade = 0;
			st->mode = FM_IDLE;
		}
	}
	break;
	case FM_FADEOUT: {
		err = fadeframe(st, af, st->mode);
		if (st->i_fade >= st->n_fade) {
			st->i_fade = 0;
			st->mode = FM_MIX;
		}
	}
	break;
	case FM_MIX: {
		if (!st->ausrc) {
			start_ausrc(st);
			clear_frame(st, af);
		}
		else if (aubuf_cur_size(st->aubuf) >= st->nbytes_strm) {
			st->aubuf_started = true;
			aubuf_read(st->aubuf,
				   (void *) st->rbuf, st->nbytes_strm);
			/* now mix */
			err = mixframe(st, af);
		}
		else {
			clear_frame(st, af);
		}
	}
	break;
	default:
	break;
	}

	return err;
}


static int encode(struct aufilt_enc_st *aufilt_enc_st, struct auframe *af)
{
	struct mixausrc_enc *mixausrc = (void *)aufilt_enc_st;

	return process(&mixausrc->st, af);
}


static int decode(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct mixausrc_dec *mixausrc = (void *)aufilt_dec_st;

	return process(&mixausrc->st, af);
}


static float conv_volume(const struct pl *pl)
{
	uint32_t percent = pl_u32(pl);
	float v = percent / 100.f;
	if (v < 0.f)
		v = 0.f;
	else if (v > 1.f)
		v = 1.f;

	return v;
}


static void print_usage(const char *name)
{
	info("mixausrc: Missing parameters. Usage:\n"
			"%s <module> <param> [minvol] [ausvol]\n"
			"module  The audio source module.\n"
			"param   The audio source parameter. If this is an"
			" audio file,\n"
			"        then you have to specify the full path.\n"
			"minvol  The minimum fade out mic volume (0-100).\n"
			"ausvol  The audio source volume (0-100).\n", name);
}


static int start_process(struct mixstatus* st, const char *name,
		const struct cmd_arg *carg)
{
	int err = 0;
	struct pl pl1 = PL_INIT;
	struct pl pl2 = PL_INIT;
	struct pl pl3 = PL_INIT;
	struct pl pl4 = PL_INIT;

	if (!carg || !str_isset(carg->prm)) {
		print_usage(name);
		return EINVAL;
	}

	if (st->mode != FM_IDLE && st->mode != FM_FADEIN) {
		warning("mixausrc: %s is not possible while mode is %s\n",
				name, str_mixmode(st->mode));
		return EINVAL;
	}

	err = re_regex(carg->prm, strlen(carg->prm), "[^ ]* [^ ]* [^ ]* [^ ]*",
			&pl1, &pl2, &pl3, &pl4);
	if (err)
		err = re_regex(carg->prm, strlen(carg->prm), "[^ ]* [^ ]*",
			&pl1, &pl2);

	if (err) {
		print_usage(name);
		return err;
	}

	st->module = mem_deref(st->module);
	st->param  = mem_deref(st->param);

	err  = pl_strdup(&st->module, &pl1);
	err |= pl_strdup(&st->param,  &pl2);
	if (err)
		return err;

	/* Fading arguments */
	st->minvol = pl_isset(&pl3) ? conv_volume(&pl3) : 0.;
	st->ausvol = pl_isset(&pl4) ? conv_volume(&pl4) : 1.;
	st->i_fade = 0;
	st->n_fade = (DEFAULT_FADE_TIME * st->ausrc_prm.srate) / 1000;
	st->delta_fade = (1.0 - st->minvol) / st->n_fade;

	stop_ausrc(st);
	ausrc_prm_aufilt(&st->ausrc_prm, &st->prm);
	st->nextmode = FM_FADEOUT;

	return 0;
}


static int enc_mix_start(struct re_printf *pf, void *arg);
static int dec_mix_start(struct re_printf *pf, void *arg);
static int enc_mix_stop(struct re_printf *pf, void *unused);
static int dec_mix_stop(struct re_printf *pf, void *unused);

/**
 * \struct cmdv
 * The commands for this module.
 * mixausrc_enc_start and mixausrc_dec_start have four parameters separated by
 * blank.
 *	- Name of the audio source.
 *	- A string that is passed to the audio source. (E.g. filename)
 *	- A volume value between 0 and 100% for the original stream. The stream
 *	  is faded out from 100% down to the specified volume.
 *	- A volume value between 0 and 100% for the specified audio source. The
 *	  audio source is played (from beginning) with the specified volume.
 *
 *	E.g. "auogg /usr/share/sounds/ring.ogg 10 90"
 */
static const struct cmd cmdv[] = {

{"mixausrc_enc_start", 0, CMD_PRM, "Start mixing audio source into encoding"
	" stream.", enc_mix_start},
{"mixausrc_dec_start", 0, CMD_PRM, "Start mixing audio source into decoding"
	" stream.", dec_mix_start},
{"mixausrc_enc_stop",  0, 0, "Stop mixing of encoding stream.", enc_mix_stop},
{"mixausrc_dec_stop",  0, 0, "Stop mixing of decoding stream.", dec_mix_stop},

};


/**
 * Start mixing audio source into encoding stream.
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument. See \ref cmdv !
 *
 * @return	0 if success, otherwise error code.
 */
static int enc_mix_start(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct mixausrc_enc *st;
	(void)pf;

	if (!list_count(&encs)) {
		warning("mixausrc: no active call\n");
		return EINVAL;
	}

	st = encs.head->data;

	debug("mixausrc: %s\n", __func__);
	return start_process(&st->st, cmdv[0].name, carg);
}


/**
 * Start mixing audio source into decoding stream.
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument. See \ref cmdv !
 *
 * @return	0 if success, otherwise error code.
 */
static int dec_mix_start(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct mixausrc_dec *dec;
	(void)pf;

	if (!list_count(&decs)) {
		warning("mixausrc: no active call\n");
		return EINVAL;
	}

	dec = decs.head->data;

	debug("mixausrc: %s\n", __func__);
	return start_process(&dec->st, cmdv[1].name, carg);
}


static int stop_process(struct mixstatus *st)
{

	st->nextmode = FM_FADEIN;
	return 0;
}


/**
 * Stop mixing of encoding stream.
 *
 * @param pf		Print handler for debug output
 * @param unused	Not used.
 *
 * @return	0 if success, otherwise error code.
 */
static int enc_mix_stop(struct re_printf *pf, void *unused)
{
	struct mixausrc_enc *enc;
	(void)pf;
	(void)unused;

	if (!list_count(&decs))
		return EINVAL;

	enc = encs.head->data;

	debug("mixausrc: %s\n", __func__);
	return stop_process(&enc->st);
}


/**
 * Stop mixing of decoding stream.
 *
 * @param pf		Print handler for debug output
 * @param unused	Not used.
 *
 * @return	0 if success, otherwise error code.
 */
static int dec_mix_stop(struct re_printf *pf, void *unused)
{
	struct mixausrc_dec *dec;
	(void)pf;
	(void)unused;

	if (!list_count(&decs))
		return EINVAL;

	dec = decs.head->data;

	debug("mixausrc: %s\n", __func__);
	return stop_process(&dec->st);
}


static struct aufilt mixausrc = {
	LE_INIT, "mixausrc", encode_update, encode, decode_update, decode
};


static int module_init(void)
{
	int err;
	aufilt_register(baresip_aufiltl(), &mixausrc);

	/* register commands */
	err  = cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
	return err;
}


static int module_close(void)
{
	cmd_unregister(baresip_commands(), cmdv);
	aufilt_unregister(&mixausrc);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(mixausrc) = {
	"mixausrc",
	"filter",
	module_init,
	module_close
};

