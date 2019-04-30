/**
 * @file idlepipe.c  Configurable audio pipeline running during idle state
 * (outside call).
 *
 * Copyright (C) 2019 commend.com - Christian Spielberger
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

#define DEBUG_MODULE "idlepipe"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


/**
 * @defgroup idlepipe idlepipe
 *
 * Application module that builds a configurable audio pipeline when the
 * call idle state is entered, so when the last call terminates. This idle
 * pipeline is shut down when an incoming or outgoing call starts.
 *
 * The idle pipeline uses default audio source and audio player configured by
 * audio_source resp. audio_player. The audio player can be deactivated, if not
 * needed for the application.
 *
 * Specific filters can be specified.
 *
 * The following commands are available:
 \verbatim
 /idlepipe_enable samplerate channels play proceed filter1,filter2,...,filter_n
		Enables the idle pipeline with specified filters. Params:
		samplerate
		channels
		play	bool T/F. With or without playback (decode path).
		proceed bool T/F. Should pipeline proceed when call terminates?
		filters A comma separated list of filters.

 /idlepipe_disable  Disables the idle pipeline.
 \endverbatim
 */


/* Configurable items */
#define PTIME 20


/** Audio pipeline */
struct audio_pipe {
	struct ausrc_st *ausrc;
	struct auplay_st *auplay;
	struct aufilt_prm fprm;
	struct ausrc_prm ausrc_prm;
	char *filters;
	struct list enc_filtl;
	struct list dec_filtl;
	int16_t *sampv;
	size_t sampc;
	size_t num_bytes;
	enum aufmt fmt;
	bool play;
	uint32_t ptime;
	uint64_t timestamp;           /**< Timestamp in AUDIO_TIMEBASE units */
	bool proceed;

	uint32_t call_count;
};


static struct audio_pipe *gap = NULL;


static int audio_pipe_start(struct audio_pipe *ap);
static int audio_pipe_stop(struct audio_pipe *ap);
static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg);


static void audio_pipe_destructor(void *arg)
{
	struct audio_pipe *ap = arg;
	uag_event_unregister(ua_event_handler);

	audio_pipe_stop(ap);

	mem_deref(ap->filters);
}


/**
 * TODO: The following case will lead to a SEGV!
 * If the idlepipe is initialized in the wrong order compared to the filters.
 * More concretely: When a filter gets destructed before the idlepipe, this
 * filter still is referred to in the enc_filtl list.
 *
 * It is necessary to check the availability of the filter before processing
 * it.
 *
 * Also, if a filter gets destructed while the ausrc thread is processing
 * data via the filter a crash is the consequence.
 * So while processing of the filter another thread MUST be blocked from
 * unloading the filter.
 */
static void read_handler(struct auframe *af, void *arg)
{
	struct audio_pipe *ap = arg;
	struct le *le;
	struct aufilt_enc_st *st;
	int err = 0;
	size_t sampc = af->sampc;
	struct auframe afc;

	if (af->fmt != AUFMT_S16LE) {
		warning("idlepipe: skipping source data due to incompatible "
			"format");
		return;
	}

	if (sampc != ap->sampc || !ap->sampv) {
		ap->sampc = sampc;
		ap->num_bytes = auframe_size(af);
		ap->sampv = mem_deref(ap->sampv);
		ap->sampv = mem_zalloc(ap->num_bytes, NULL);
	}

	memcpy(ap->sampv, af->sampv, ap->num_bytes);
	afc.sampv = ap->sampv;
	afc.sampc = sampc;
	afc.fmt = af->fmt;
	afc.timestamp = af->timestamp;

	/* Process exactly one audio-frame in list order */
	for (le = ap->enc_filtl.head; le; le = le->next) {
		st = le->data;

		if (st && st->af && st->af->ench)
			err |= st->af->ench(st, &afc);
	}

	if (err)
		warning("idlepipe: encode data missing. (%m)\n", err);
}


/*static void write_handler(void *sampv, size_t sampc, void *arg)*/
static void write_handler(struct auframe *af, void *arg)
{
	struct audio_pipe *ap = arg;
	struct le *le;
	size_t num_bytes;
	struct aufilt_dec_st *st;
	size_t samples = 0;
	int err = 0;

	ap->fmt = af->fmt;
	if (ap->fmt != AUFMT_S16LE) {
		warning("idlepipe: skipping play data due to incompatible "
			"format");
		return;
	}

	num_bytes = auframe_size(af);

	/* put silence into decoding pipe */
	memset(af->sampv, 0, num_bytes);
	af->timestamp = samples * AUDIO_TIMEBASE
			             / (ap->fprm.srate * ap->fprm.ch);
	samples += af->sampc;

	for (le = ap->dec_filtl.head; le; le = le->next) {
		st = le->data;

		if (st && st->af && st->af->dech)
			err |= st->af->dech(st, af);
	}

	if (err)
		warning("idlepipe: decode data missing. (%m)\n", err);
}


static void error_handler(int err, const char *str, void *arg)
{
	(void)arg;
	warning("idlepipe: ausrc error: %m (%s)\n", err, str);
	gap = mem_deref(gap);
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	uint32_t cnt;
	struct audio_pipe *ap = arg;

	(void) ua;
	(void) call;
	(void) prm;

	/*
	Only react for Call Closed events. The rest of the handling has to be
	be done in the application or in an application module.
	*/
	switch (ev) {
	case UA_EVENT_CALL_CLOSED:
		cnt = uag_call_count();
		if (cnt == 1 && ap->proceed) {
			/* start idle pipeline */
			audio_pipe_start(ap);
		}
		ap->call_count = cnt - 1;
		break;

	default:
		break;
	}
}


static int audio_pipe_filtl_append(struct audio_pipe *ap, const char *b,
		const char *e)
{
	struct list *filtl;
	struct le *le;
	struct pl pl;
	void *ctx = NULL;
	int err = 0;

	pl.p = b;
	pl.l = e - b;
	filtl = baresip_aufiltl();
	for (le = filtl->head; le; le = le->next) {
		struct aufilt_enc_st *encst = NULL;
		struct aufilt_dec_st *decst = NULL;
		struct aufilt *af = le->data;
		bool append = false;

		if (!pl_strcmp(&pl, af->name)) {
			info("idlepipe: append filter %r\n", &pl);
			if (af->encupdh) {
				err = af->encupdh(&encst, &ctx, af, &ap->fprm,
						  NULL);
				if (err) {
					warning("idlepipe: encoder %r update "
						"failed (%m)\n", &pl, err);
					return err;
				}

				encst->af = af;
				list_append(&ap->enc_filtl, &encst->le, encst);
				append = true;
			}

			if (af->decupdh) {
				err = af->decupdh(&decst, &ctx, af, &ap->fprm,
						  NULL);
				if (err) {
					warning("idlepipe: decoder %r update "
						"failed (%m)\n", &pl, err);
					return err;
				}

				decst->af = af;
				list_append(&ap->dec_filtl, &decst->le, decst);
				append = true;
			}
		}

		if (append)
			return 0;
	}

	warning("idlepipe: could not find module %r.\n", &pl);
	return EINVAL;
}


static int audio_pipe_play(struct audio_pipe *ap, bool en)
{
	struct auplay_prm auplay_prm;
	const struct config *cfg;
	int err = 0;

	cfg = conf_config();
	if (!en) {
		info("idlepipe: remove playback\n");
		ap->auplay = mem_deref(ap->auplay);
		return 0;
	}

	info("idlepipe: add playback\n");
	/* audio player must be stopped first */
	ap->auplay = mem_deref(ap->auplay);

	auplay_prm.srate      = ap->fprm.srate;
	auplay_prm.ch         = ap->fprm.ch;
	auplay_prm.ptime      = ap->ptime;
	auplay_prm.fmt        = ap->fmt;
	err = auplay_alloc(&ap->auplay, baresip_auplayl(),
			cfg->audio.play_mod, &auplay_prm,
			cfg->audio.play_dev, write_handler, ap);
	if (err) {
		warning("idlepipe: auplay %s,%s failed: %m\n",
				cfg->audio.play_mod, cfg->audio.play_dev,
				err);
		return err;
	}

	return 0;
}


static int audio_pipe_start(struct audio_pipe *ap)
{
	const char *p, *c, *end;
	const struct config *cfg = conf_config();
	int err = 0;

	info("idlepipe: start idle pipeline (play=%d, filters=%s)\n", ap->play,
			ap->filters);
	list_flush(&ap->enc_filtl);
	list_flush(&ap->dec_filtl);
	if (str_isset(ap->filters)) {
		p = ap->filters;
		end = ap->filters + strlen(ap->filters);
		for (c = ap->filters; c < end; c++) {
			if (*c == ',') {
				err = audio_pipe_filtl_append(ap, p, c);
				if (err)
					return err;
				p = c + 1;
			}
		}

		if (p < end)
			err = audio_pipe_filtl_append(ap, p, end);
	}

	if (ap->play)
		err = audio_pipe_play(ap, true);

	if (err)
		return err;

	/* audio source must be stopped first */
	ap->ausrc  = mem_deref(ap->ausrc);
	ap->ausrc_prm.srate      = ap->fprm.srate;
	ap->ausrc_prm.ch         = ap->fprm.ch;
	ap->ausrc_prm.ptime      = ap->ptime;
	ap->ausrc_prm.fmt        = ap->fmt;
	err = ausrc_alloc(&ap->ausrc, baresip_ausrcl(),
			  cfg->audio.src_mod,
			  &ap->ausrc_prm, cfg->audio.src_dev,
			  read_handler, error_handler, ap);
	if (err) {
		warning("idlepipe: ausrc %s,%s failed: %m\n",
			cfg->audio.src_mod, cfg->audio.src_dev, err);
	}

	return err;
}


static int audio_pipe_stop(struct audio_pipe *ap)
{
	info("idlepipe: stop idle pipeline\n");
	ap->ausrc= mem_deref(ap->ausrc);
	list_flush(&ap->enc_filtl);
	list_flush(&ap->dec_filtl);
	ap->auplay = mem_deref(ap->auplay);
	ap->sampv = mem_deref(ap->sampv);

	return 0;
}


static int audio_pipe_reset(struct audio_pipe *ap, uint32_t srate, uint32_t ch,
		const struct pl *filters, bool play)
{
	const struct config *cfg = conf_config();
	int err;

	if (!cfg)
		return ENOENT;

	if (cfg->audio.src_fmt != cfg->audio.play_fmt) {
		warning("idlepipe: ausrc_format and auplay_format"
			" must be the same\n");
		return EINVAL;
	}

	ap->play   = play;
	ap->fmt    = cfg->audio.src_fmt;

	ap->fprm.srate = srate;
	ap->fprm.ch    = ch;
	ap->ptime      = PTIME;
	ap->timestamp  = 0;

	pl_strdup(&ap->filters, filters);
	err = uag_event_register(ua_event_handler, ap);
	if (err)
		return err;

	ap->call_count = uag_call_count();
	if (ap->call_count)
		return 0;

	err = audio_pipe_start(ap);
	return err;
}


static int audio_pipe_alloc(struct audio_pipe **alp,
			    uint32_t srate, uint32_t ch,
			    const struct pl *filters, bool play)
{
	struct audio_pipe *ap;
	int err;

	ap = mem_zalloc(sizeof(*ap), audio_pipe_destructor);
	if (!ap)
		return ENOMEM;

	err = audio_pipe_reset(ap, srate, ch, filters, play);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(ap);
	else
		*alp = ap;

	return err;
}


static int print_usage(struct re_printf *pf)
{
	return re_hprintf(pf, "Usage: /idlepipe"
		" <samplerate> <channels> <play> <proceed> <filters>\n"
		"  samplerate    In Hz.\n"
		"  channels      The number of channels can be 1 or 2.\n"
		"  play          \"T\"/\"F\" \"T\" if audio_player should be "
				"started.\n"
		"  proceed       \"T\"/\"F\" \"T\" if idlepipe should proceed "
				"after interrupted by call or file playback.\n"
		"  filters       A comma-separated list of filter names.\n");
}


/*
 * Start the audio pipeline
 */
static int audio_pipe_enable(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct pl pl_srate, pl_ch, pl_play, pl_pro, pl_filters;
	uint32_t srate, ch;
	bool play, proceed;
	int err;

	if (gap)
		return re_hprintf(pf, "idlepipe: idle pipeline already "
				  "running.\n");

	err = re_regex(carg->prm, str_len(carg->prm),
		       "[0-9]+ [0-9]+ [~]1 [~]1 [~]*",
		       &pl_srate, &pl_ch, &pl_play, &pl_pro, &pl_filters);
	if (err)
		return print_usage(pf);

	srate = pl_u32(&pl_srate);
	ch    = pl_u32(&pl_ch);
	play  = !pl_strcmp(&pl_play, "T") || !pl_strcmp(&pl_play, "1");
	if (!srate || !ch)
		return re_hprintf(pf, "invalid samplerate or channels\n");
	proceed = !pl_strcmp(&pl_pro, "T") || !pl_strcmp(&pl_pro, "1");

	err = audio_pipe_alloc(&gap, srate, ch, &pl_filters, play);
	if (err) {
		warning("idlepipe: alloc failed %m\n", err);
	}
	else {
		(void)re_hprintf(pf, "idlepipe: enabled idle pipeline\n");
	}

	if (gap)
		gap->proceed = proceed;

	return err;
}


static int audio_pipe_disable(struct re_printf *pf, void *arg)
{
	(void)arg;

	if (gap) {
		(void)re_hprintf(pf, "idlepipe: disable idle pipeline\n");
		uag_event_unregister(ua_event_handler);
		gap = mem_deref(gap);
	}

	return 0;
}


static int audio_pipe_addplay(struct re_printf *pf, void *arg)
{
	int err = 0;

	(void)arg;
	if (!gap) {
		re_hprintf(pf, "idlepipe: enable idlepipe first\n");
		return EINVAL;
	}

	if (gap->play)
		return 0;

	err = audio_pipe_play(gap, true);
	if (err)
		return err;

	gap->play = true;
	return 0;
}


static int audio_pipe_rmplay(struct re_printf *pf, void *arg)
{
	int err = 0;

	(void)arg;
	if (!gap) {
		re_hprintf(pf, "idlepipe: enable idlepipe first\n");
		return EINVAL;
	}

	if (!gap->play)
		return 0;

	err = audio_pipe_play(gap, false);
	if (err)
		return err;

	gap->play = false;
	return 0;
}


static const struct cmd cmdv[] = {
	{"idlepipe_enable", 0, CMD_PRM, "Enables idle audio pipeline"
		" <samplerate> <channels> <play> <proceed> <filters>",
		audio_pipe_enable},
	{"idlepipe_disable", 0, 0, "Disables audio pipeline",
		audio_pipe_disable },
	{"idlepipe_addplay", 0, 0, "Adds playback to idle pipeline",
		audio_pipe_addplay},
	{"idlepipe_rmplay", 0, 0, "Removes playback from idle pipeline",
		audio_pipe_rmplay},
};


static int module_init(void)
{
	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	audio_pipe_disable(NULL, NULL);
	cmd_unregister(baresip_commands(), cmdv);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(idlepipe) = {
	"idlepipe",
	"application",
	module_init,
	module_close,
};
