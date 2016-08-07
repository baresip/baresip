/**
 * @file auloop.c  Audio loop
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup auloop auloop
 *
 * Application module for testing audio drivers
 *
 * The audio loop will connect the AUSRC device to the AUPLAY device
 * so that a local loopback audio can be heard. Different audio parameters
 * can be tested, such as sampling rate and number of channels.
 *
 * The following commands are available:
 \verbatim
 a       Start audio-loop
 A       Stop audio-loop
 \endverbatim
 */


/* Configurable items */
#define PTIME 20


/** Audio Loop */
struct audio_loop {
	uint32_t index;
	struct aubuf *ab;
	struct ausrc_st *ausrc;
	struct auplay_st *auplay;
	const struct aucodec *ac;
	struct auenc_state *enc;
	struct audec_state *dec;
	int16_t *sampv;
	size_t sampc;
	struct tmr tmr;
	uint32_t srate;
	uint32_t ch;

	uint32_t n_read;
	uint32_t n_write;
};

static const struct {
	uint32_t srate;
	uint32_t ch;
} configv[] = {
	{ 8000, 1},
	{16000, 1},
	{32000, 1},
	{44100, 1},
	{48000, 1},
	{ 8000, 2},
	{16000, 2},
	{32000, 2},
	{44100, 2},
	{48000, 2},
};

static struct audio_loop *gal = NULL;
static char aucodec[64];


static void auloop_destructor(void *arg)
{
	struct audio_loop *al = arg;

	tmr_cancel(&al->tmr);
	mem_deref(al->ausrc);
	mem_deref(al->auplay);
	mem_deref(al->sampv);
	mem_deref(al->ab);
	mem_deref(al->enc);
	mem_deref(al->dec);
}


static void print_stats(struct audio_loop *al)
{
	double rw_ratio = 0.0;

	if (al->n_write)
		rw_ratio = 1.0 * al->n_read / al->n_write;

	(void)re_fprintf(stderr, "\r%uHz %dch "
			 " n_read=%u n_write=%u rw_ratio=%.2f",
			 al->srate, al->ch,
			 al->n_read, al->n_write, rw_ratio);

	if (str_isset(aucodec))
		(void)re_fprintf(stderr, " codec='%s'", aucodec);
}


static void tmr_handler(void *arg)
{
	struct audio_loop *al = arg;

	tmr_start(&al->tmr, 100, tmr_handler, al);
	print_stats(al);
}


static int codec_read(struct audio_loop *al, int16_t *sampv, size_t sampc)
{
	uint8_t x[2560];
	size_t xlen = sizeof(x);
	int err;

	aubuf_read_samp(al->ab, al->sampv, al->sampc);

	err = al->ac->ench(al->enc, x, &xlen, al->sampv, al->sampc);
	if (err)
		goto out;

	if (al->ac->dech) {
		err = al->ac->dech(al->dec, sampv, &sampc, x, xlen);
		if (err)
			goto out;
	}
	else {
		info("auloop: no decode handler\n");
	}

 out:

	return err;
}


static void read_handler(const int16_t *sampv, size_t sampc, void *arg)
{
	struct audio_loop *al = arg;
	int err;

	++al->n_read;

	err = aubuf_write_samp(al->ab, sampv, sampc);
	if (err) {
		warning("auloop: aubuf_write: %m\n", err);
	}
}


static void write_handler(int16_t *sampv, size_t sampc, void *arg)
{
	struct audio_loop *al = arg;
	int err;

	++al->n_write;

	/* read from beginning */
	if (al->ac) {
		err = codec_read(al, sampv, sampc);
		if (err) {
			warning("auloop: codec_read error "
				"on %zu samples (%m)\n", sampc, err);
		}
	}
	else {
		aubuf_read_samp(al->ab, sampv, sampc);
	}
}


static void error_handler(int err, const char *str, void *arg)
{
	(void)arg;
	warning("auloop: ausrc error: %m (%s)\n", err, str);
	gal = mem_deref(gal);
}


static void start_codec(struct audio_loop *al, const char *name)
{
	struct auenc_param prm = {PTIME};
	int err;

	al->ac = aucodec_find(name,
			      configv[al->index].srate,
			      configv[al->index].ch);
	if (!al->ac) {
		warning("auloop: could not find codec: %s\n", name);
		return;
	}

	if (al->ac->encupdh) {
		err = al->ac->encupdh(&al->enc, al->ac, &prm, NULL);
		if (err) {
			warning("auloop: encoder update failed: %m\n", err);
		}
	}

	if (al->ac->decupdh) {
		err = al->ac->decupdh(&al->dec, al->ac, NULL);
		if (err) {
			warning("auloop: decoder update failed: %m\n", err);
		}
	}
}


static int auloop_reset(struct audio_loop *al)
{
	struct auplay_prm auplay_prm;
	struct ausrc_prm ausrc_prm;
	const struct config *cfg = conf_config();
	int err;

	if (!cfg)
		return ENOENT;

	/* Optional audio codec */
	if (str_isset(aucodec))
		start_codec(al, aucodec);

	/* audio player/source must be stopped first */
	al->auplay = mem_deref(al->auplay);
	al->ausrc  = mem_deref(al->ausrc);

	al->sampv  = mem_deref(al->sampv);
	al->ab     = mem_deref(al->ab);

	al->srate = configv[al->index].srate;
	al->ch    = configv[al->index].ch;

	if (str_isset(aucodec)) {
		al->sampc = al->srate * al->ch * PTIME / 1000;
		al->sampv = mem_alloc(al->sampc * 2, NULL);
		if (!al->sampv)
			return ENOMEM;
	}

	info("Audio-loop: %uHz, %dch\n", al->srate, al->ch);

	err = aubuf_alloc(&al->ab, 320, 0);
	if (err)
		return err;

	auplay_prm.srate      = al->srate;
	auplay_prm.ch         = al->ch;
	auplay_prm.ptime      = PTIME;
	err = auplay_alloc(&al->auplay, cfg->audio.play_mod, &auplay_prm,
			   cfg->audio.play_dev, write_handler, al);
	if (err) {
		warning("auloop: auplay %s,%s failed: %m\n",
			cfg->audio.play_mod, cfg->audio.play_dev,
			err);
		return err;
	}

	ausrc_prm.srate      = al->srate;
	ausrc_prm.ch         = al->ch;
	ausrc_prm.ptime      = PTIME;
	err = ausrc_alloc(&al->ausrc, NULL, cfg->audio.src_mod,
			  &ausrc_prm, cfg->audio.src_dev,
			  read_handler, error_handler, al);
	if (err) {
		warning("auloop: ausrc %s,%s failed: %m\n", cfg->audio.src_mod,
			cfg->audio.src_dev, err);
		return err;
	}

	return err;
}


static int audio_loop_alloc(struct audio_loop **alp)
{
	struct audio_loop *al;
	int err;

	al = mem_zalloc(sizeof(*al), auloop_destructor);
	if (!al)
		return ENOMEM;

	tmr_start(&al->tmr, 100, tmr_handler, al);

	err = auloop_reset(al);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(al);
	else
		*alp = al;

	return err;
}


static int audio_loop_cycle(struct audio_loop *al)
{
	int err;

	++al->index;

	if (al->index >= ARRAY_SIZE(configv)) {
		gal = mem_deref(gal);
		info("\nAudio-loop stopped\n");
		return 0;
	}

	err = auloop_reset(al);
	if (err)
		return err;

	info("\nAudio-loop started: %uHz, %dch\n", al->srate, al->ch);

	return 0;
}


/**
 * Start the audio loop (for testing)
 */
static int auloop_start(struct re_printf *pf, void *arg)
{
	int err;

	(void)pf;
	(void)arg;

	if (gal) {
		err = audio_loop_cycle(gal);
		if (err) {
			warning("auloop: loop cycle: %m\n", err);
		}
	}
	else {
		err = audio_loop_alloc(&gal);
		if (err) {
			warning("auloop: alloc failed %m\n", err);
		}
	}

	return err;
}


static int auloop_stop(struct re_printf *pf, void *arg)
{
	(void)arg;

	if (gal) {
		(void)re_hprintf(pf, "audio-loop stopped\n");
		gal = mem_deref(gal);
	}

	return 0;
}


static const struct cmd cmdv[] = {
	{"auloop",      'a', 0, "Start audio-loop", auloop_start },
	{"auloop_stop", 'A', 0, "Stop audio-loop",  auloop_stop  },
};


static int module_init(void)
{
	conf_get_str(conf_cur(), "auloop_codec", aucodec, sizeof(aucodec));

	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	auloop_stop(NULL, NULL);
	cmd_unregister(baresip_commands(), cmdv);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(auloop) = {
	"auloop",
	"application",
	module_init,
	module_close,
};
