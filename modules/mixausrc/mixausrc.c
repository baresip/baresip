/**
 * @file mixausrc.c  Mixes another audio source into audio stream.
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */
#include <string.h>
#include <stdlib.h>
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

enum {
	DEFAULT_FADE_TIME=160,  /**< Default fade time [ms]                  */
	PTIME=40,               /**< Packet time for ausrc reading [ms]      */
};

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
	RE_ATOMIC enum mixmode mode;    /**< Current mix mode                */
	RE_ATOMIC enum mixmode nextmode;  /**< Next mix mode                 */
	float minvol;                   /**< Minimum audio stream volume     */
	float ausvol;                   /**< Volume for mixed audio source   */
	size_t nmix;                    /**< Size of mixer buffer [bytes]    */
	uint16_t i_fade;                /**< Fade-in/-out counter            */
	uint16_t n_fade;                /**< Fade-in/-out steps              */
	float delta_fade;               /**< linear delta accumulation       */

	int16_t *mixbuf;

	/* thread ausrc */
	struct aubuf *aubuf;	        /**< Buffer for resampled ausrc      */
	struct auresamp resamp;         /**< Optional audio resampler        */
	size_t nres;                    /**< Size of resample buffer [bytes] */
	void *sampvrs;                  /**< Optional resample buffer        */
	struct aufilt_prm prm;          /**< Audio filter parameter          */
	mtx_t *mtx;                     /**< Mutex for ausrc thread          */
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


static void stop_ausrc(struct mixstatus *st);


static int init_aubuf(struct mixstatus *st)
{
	int err;
	uint32_t v = 2;
	size_t maxsz;
	size_t wishsz;

	conf_get_u32(conf_cur(), "mixausrc_wish_size", &v);
	wishsz = v * st->nres;
	maxsz = 2 * wishsz;

	st->aubuf = mem_deref(st->aubuf);

	err = aubuf_alloc(&st->aubuf, wishsz, maxsz);
	if (err) {
		warning("mixausrc: Could not allocate aubuf. wishsz=%lu, "
				"maxsz=%lu (%m)\n", wishsz, maxsz, err);
		return err;
	}

	aubuf_set_live(st->aubuf, false);

	struct pl *id = pl_alloc_str("mixausrc");
	if (!id) {
		err = ENOMEM;
		goto out;
	}

	aubuf_set_id(st->aubuf, id);
	mem_deref(id);

out:
	if (err)
		st->aubuf = mem_deref(st->aubuf);

	return err;
}


static int init_mixbuf(struct mixstatus *st)
{
	st->mixbuf = mem_deref(st->mixbuf);
	st->mixbuf = mem_zalloc(st->nmix, NULL);
	if (!st->mixbuf)
		return ENOMEM;

	return 0;
}


static int process_resamp(struct mixstatus *st, struct auframe *afres,
			  const struct auframe *afsrc)
{
	int err = 0;
	if (afsrc->fmt != AUFMT_S16LE) {
		warning("mixausrc: sample format %s not supported\n",
			aufmt_name(afsrc->fmt));
		return EINVAL;
	}

	if (!st->resamp.resample || !st->sampvrs) {
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
		st->sampvrs = mem_zalloc(st->nres, NULL);
		if (!st->sampvrs)
			return ENOMEM;
	}

	afres->sampv = st->sampvrs;
	afres->sampc = st->nres / aufmt_sample_size(st->prm.fmt);
	afres->srate = st->prm.srate;
	afres->ch    = st->prm.ch;
	err = auresamp(&st->resamp, afres->sampv, &afres->sampc,
		       afsrc->sampv, afsrc->sampc);
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


static void switch_mode(struct mixstatus *st, enum mixmode mode)
{
	if (!st || re_atomic_rlx(&st->mode) == mode)
		return;

	debug("mixausrc: mode %s --> %s\n",
	      str_mixmode(re_atomic_rlx(&st->mode)), str_mixmode(mode));
	re_atomic_rlx_set(&st->mode, mode);
}


static void ausrc_error_handler(int err, const char *str, void *arg)
{
	struct mixstatus *st = arg;
	(void)str;

	/* reached EOS of ausrc */
	debug("mixausrc: reached EOS of ausrc (%m)\n", err);
	re_atomic_rlx_set(&st->nextmode, FM_FADEIN);
	stop_ausrc(st);
}


static void ausrc_read_handler(struct auframe *afsrc, void *arg)
{
	struct mixstatus *st = arg;
	int err = 0;

	mtx_lock(st->mtx);
	if (!st->prm.srate || !st->prm.ch)
		goto out;

	ausrc_prm_af(&st->ausrc_prm, afsrc);
	if (!st->ausrc_prm.srate || !st->ausrc_prm.ch)
		goto out;

	struct auframe afres = *afsrc;
	if (st->ausrc_prm.srate != st->prm.srate ||
	    st->ausrc_prm.ch != st->prm.ch) {
		err = process_resamp(st, &afres, afsrc);
		if (err)
			goto out;
	}

	if (!st->aubuf) {
		err = init_aubuf(st);
		if (err)
			goto out;
	}

	afres.timestamp = 0;
	aubuf_write_auframe(st->aubuf, &afres);
out:
	if (err)
		re_atomic_rlx_set(&st->nextmode, FM_FADEIN);

	mtx_unlock(st->mtx);
}


static int start_ausrc(struct mixstatus *st)
{
	int err;

	mtx_lock(st->mtx);
	auresamp_init(&st->resamp);
	err = ausrc_alloc(&st->ausrc, baresip_ausrcl(), st->module,
			  &st->ausrc_prm, st->param, ausrc_read_handler,
			  ausrc_error_handler, st);

	if (!st->ausrc) {
		warning("mixausrc: Could not start audio source %s with "
				"data %s.\n", st->module, st->param);
		err = EINVAL;
		goto out;
	}

	/* now we are ready for next start_process */
	st->module = mem_deref(st->module);
	st->param = mem_deref(st->param);

	struct ausrc_prm *prm = &st->ausrc_prm;
	/* nres holds max of both streams */
	size_t sz = aufmt_sample_size(prm->fmt);
	size_t n = sz * prm->srate * prm->ch * PTIME / 1000;
	if (n > st->nres)
		st->nres = n;

out:
	if (err)
		re_atomic_rlx_set(&st->nextmode, FM_FADEIN);

	mtx_unlock(st->mtx);
	return err;
}


static void stop_ausrc(struct mixstatus *st)
{
	st->ausrc   = mem_deref(st->ausrc);
	mtx_lock(st->mtx);
	st->aubuf   = mem_deref(st->aubuf);
	st->mixbuf  = mem_deref(st->mixbuf);
	st->sampvrs = mem_deref(st->sampvrs);
	st->nres  = 0;
	st->nmix  = 0;
	mtx_unlock(st->mtx);
}


static void destruct(struct mixstatus *st)
{
	stop_ausrc(st);
	mem_deref(st->module);
	mem_deref(st->param);
	mem_deref(st->mtx);
}


static void enc_destructor(void *arg)
{
	struct mixausrc_enc *enc = (struct mixausrc_enc *) arg;

	list_unlink(&enc->le_priv);
	destruct(&enc->st);
}


static void dec_destructor(void *arg)
{
	struct mixausrc_dec *dec = (struct mixausrc_dec *)arg;

	list_unlink(&dec->le_priv);
	destruct(&dec->st);
}


static int mixstatus_init(struct mixstatus *st, struct aufilt_prm *prm)
{
	int err;
	err = mutex_alloc(&st->mtx);
	if (err)
		return err;

	stop_ausrc(st);

	re_atomic_rlx_set(&st->mode, FM_IDLE);
	st->minvol = 1.0f;
	st->ausvol = 1.0f;
	st->i_fade = 0;

	/* initialize with filter parameters */
	st->prm = *prm;
	st->ausrc_prm.ch = prm->ch;
	st->ausrc_prm.fmt = prm->fmt;
	st->ausrc_prm.srate = prm->srate;
	st->ausrc_prm.ptime = PTIME;
	return 0;
}


static int encode_update(struct aufilt_enc_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct mixausrc_enc *enc;
	(void)au;
	(void)af;
	(void)ctx;

	if (!stp || !ctx || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	enc = mem_zalloc(sizeof(*enc), enc_destructor);
	if (!enc)
		return ENOMEM;

	list_append(&encs, &enc->le_priv, enc);
	*stp = (struct aufilt_enc_st *) enc;

	return mixstatus_init(&enc->st, prm);
}


static int decode_update(struct aufilt_dec_st **stp, void **ctx,
			 const struct aufilt *af, struct aufilt_prm *prm,
			 const struct audio *au)
{
	struct mixausrc_dec *dec;
	(void)au;
	(void)af;
	(void)ctx;

	if (!stp || !ctx || !prm)
		return EINVAL;

	if (*stp)
		return 0;

	dec = mem_zalloc(sizeof(*dec), dec_destructor);
	if (!dec)
		return ENOMEM;

	list_append(&decs, &dec->le_priv, dec);
	*stp = (struct aufilt_dec_st *) dec;

	return mixstatus_init(&dec->st, prm);
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
		return (st->minvol + factor) > 1.0f ? 1.0f
						    : st->minvol + factor;
	else
		return (1.0f - factor) < st->minvol ? st->minvol
						    : 1.0f - factor;
}


static void fade_int16(struct mixstatus *st, int16_t *data, size_t n,
	enum mixmode dir)
{
	for (size_t i = 0; (i < n) && (st->i_fade < st->n_fade); ++i)
		data[i] = (int16_t)(data[i] * fade_linear(st, dir));
}


static void fade_float(struct mixstatus *st, float *data, size_t n,
	enum mixmode dir)
{
	for (size_t i = 0; (i < n) && (st->i_fade < st->n_fade); ++i)
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


static void clear_int16(struct mixstatus *st, int16_t *data, size_t n)
{
	for (size_t i = 0; i < n; ++i)
		data[i] = (int16_t)(data[i] * st->minvol);
}


static void clear_float(struct mixstatus *st, float *data, size_t n)
{
	for (size_t i = 0; i < n; ++i)
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
				(st->ausvol * st->mixbuf[i]));
}


static void mix_float(struct mixstatus *st, float *data, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		data[i] = data[i] * st->minvol +
			(st->ausvol * st->mixbuf[i]);
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


static void aufilt_prm_update(struct mixstatus *st, struct auframe *af)
{
	if (st->prm.srate == af->srate &&
	    st->prm.ch    == af->ch   &&
	    st->prm.fmt   == (int) af->fmt)
		return;

	warning("mixausrc: auframe parameters do not match filter "
		"parameters\n");
	mtx_lock(st->mtx);
	st->prm.srate = af->srate;
	st->prm.ch    = af->ch;
	st->prm.fmt   = (int) af->fmt;
	mtx_unlock(st->mtx);
}


static int process(struct mixstatus *st, struct auframe *af)
{
	int err = 0;

	aufilt_prm_update(st, af);

	if (st->mode == FM_FADEOUT && st->i_fade == st->n_fade)
		st->mode = FM_MIX;

	/* process nextmode */
	if (re_atomic_rlx(&st->mode) == FM_MIX &&
	    re_atomic_rlx(&st->nextmode) == FM_FADEOUT)
		re_atomic_rlx_set(&st->nextmode, FM_NONE);

	else if (re_atomic_rlx(&st->mode) == FM_IDLE &&
		 re_atomic_rlx(&st->nextmode) == FM_FADEIN)
		re_atomic_rlx_set(&st->nextmode, FM_NONE);

	else if (re_atomic_rlx(&st->nextmode) != FM_NONE) {
		/* a command was invoked */
		/* process nextmode */
		switch_mode(st, re_atomic_rlx(&st->nextmode));
		re_atomic_rlx_set(&st->nextmode, FM_NONE);
	}

	enum mixmode mode = re_atomic_rlx(&st->mode);
	switch (mode) {
	case FM_FADEIN: {
		err = fadeframe(st, af, mode);
		if (st->i_fade >= st->n_fade) {
			st->i_fade = 0;
			switch_mode(st, FM_IDLE);
		}
	}
	break;
	case FM_FADEOUT: {
		err = fadeframe(st, af, mode);
		if (st->i_fade >= st->n_fade) {
			st->i_fade = 0;
			switch_mode(st, FM_MIX);
		}
	}
	break;
	case FM_MIX: {
		size_t n = auframe_size(af);
		size_t sz = aufmt_sample_size(af->fmt);
		if (!st->nres) {
			st->nres = sz * au_calc_nsamp(af->srate,
						      af->ch, PTIME);
			st->nmix = n;
		}

		if (!st->nres || !st->nmix) {
			warning("mixausrc: nres or nmix is zero\n");
			return EINVAL;
		}

		if (!st->ausrc) {
			start_ausrc(st);
			clear_frame(st, af);
		}
		else {
			if (!st->mixbuf || n > st->nmix) {
				st->nmix = n;
				init_mixbuf(st);
			}

			struct auframe afmix;
			auframe_init(&afmix, af->fmt, st->mixbuf, af->sampc,
				     af->srate, af->ch);
			aubuf_read_auframe(st->aubuf, &afmix);
			/* now mix */
			err = mixframe(st, af);
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
	struct mixausrc_enc *enc= (struct mixausrc_enc *)aufilt_enc_st;

	return process(&enc->st, af);
}


static int decode(struct aufilt_dec_st *aufilt_dec_st, struct auframe *af)
{
	struct mixausrc_dec *dec = (struct mixausrc_dec *)aufilt_dec_st;

	return process(&dec->st, af);
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

	enum mixmode mode = re_atomic_rlx(&st->mode);
	if (mode != FM_IDLE) {
		warning("mixausrc: %s is not possible while mode is %s\n",
				name, str_mixmode(mode));
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
	st->minvol = pl_isset(&pl3) ? conv_volume(&pl3) : 0.0f;
	st->ausvol = pl_isset(&pl4) ? conv_volume(&pl4) : 1.0f;
	st->i_fade = 0;
	st->n_fade = (DEFAULT_FADE_TIME * st->prm.srate) / 1000;
	st->delta_fade = (1.0f - st->minvol) / st->n_fade;

	stop_ausrc(st);
	ausrc_prm_aufilt(&st->ausrc_prm, &st->prm);
	re_atomic_rlx_set(&st->nextmode, FM_FADEOUT);

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
	struct mixausrc_enc *enc;
	(void)pf;

	if (!list_count(&encs)) {
		warning("mixausrc: no active call\n");
		return EINVAL;
	}

	enc = encs.head->data;

	debug("mixausrc: %s\n", __func__);
	return start_process(&enc->st, cmdv[0].name, carg);
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

	re_atomic_rlx_set(&st->nextmode, FM_FADEIN);
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

	if (!list_count(&encs))
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


static struct aufilt mixausrc = {.name	  = "mixausrc",
				 .encupdh = encode_update,
				 .ench	  = encode,
				 .decupdh = decode_update,
				 .dech	  = decode};


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

