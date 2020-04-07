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
 /auloop <samplerate> <channels>    Start audio-loop
 /auloop_stop                       Stop audio-loop
 \endverbatim
 */


/* Configurable items */
#define PTIME 20


/** Audio Loop */
struct audio_loop {
	struct aubuf *aubuf;
	struct ausrc_st *ausrc;
	struct auplay_st *auplay;
	struct lock *lock;
	struct tmr tmr;
	uint32_t srate;
	uint8_t ch;
	enum aufmt fmt;
	bool started;

	size_t aubuf_maxsz;
	uint64_t aubuf_overrun;
	uint64_t aubuf_underrun;

	struct stats {
		uint64_t n_samp;
		uint64_t n_frames;
	} stats_src, stats_play;
};


static struct audio_loop *gal = NULL;


static inline uint32_t calc_nsamp(uint32_t srate, uint8_t channels,
				  uint16_t ptime)
{
	return srate * channels * ptime / 1000;
}


static int print_summary(struct re_printf *pf, struct audio_loop *al)
{
	const double scale = al->srate * al->ch;
	int err;

	err  = re_hprintf(pf, "~~~~~ Audioloop summary: ~~~~~\n");
	err |= re_hprintf(pf, "%u Hz %uch %s\n\n",
			  al->srate, al->ch, aufmt_name(al->fmt));

	/* Source */
	if (al->ausrc) {
		struct ausrc *as = ausrc_get(al->ausrc);
		const char *name = as->name;
		const struct stats *stats = &al->stats_src;
		double dur;

		al->ausrc = mem_deref(al->ausrc);

		dur = (double)stats->n_samp / scale;

		err |= re_hprintf(pf,
				  "* Source\n"
				  "  module      %s\n"
				  "  samples     %llu\n"
				  "  duration    %.3f sec\n"
				  "  frames      %llu (avg ptime %.2fms)\n"
				  "\n"
				  ,
				  name,
				  stats->n_samp,
				  dur,
				  al->stats_src.n_frames,
				  1000.0*dur / (double)al->stats_src.n_frames
				  );
	}

	if (al->aubuf) {

		err |= re_hprintf(pf,
				  "* Aubuf\n"
				  "  overrun     %llu\n"
				  "  underrun    %llu\n"
				  "\n"
				  ,
				  al->aubuf_overrun,
				  al->aubuf_underrun);
	}

	/* Player */
	if (al->auplay) {
		struct auplay *ap = auplay_get(al->auplay);
		const char *name = ap->name;
		const struct stats *stats = &al->stats_play;
		double dur;

		/* stop device first */
		al->auplay = mem_deref(al->auplay);

		dur = (double)stats->n_samp / scale;

		err |= re_hprintf(pf,
				  "* Player\n"
				  "  module      %s\n"
				  "  samples     %llu\n"
				  "  duration    %.3f sec\n"
				  "  frames      %llu (avg ptime %.2fms)\n"
				  "\n"
				  ,
				  name,
				  stats->n_samp,
				  dur,
				  stats->n_frames,
				  1000.0*dur / (double)stats->n_frames
				  );
	}

	return err;
}


static void auloop_destructor(void *arg)
{
	struct audio_loop *al = arg;

	if (al->started)
		re_printf("%H\n", print_summary, al);

	tmr_cancel(&al->tmr);
	mem_deref(al->ausrc);
	mem_deref(al->auplay);
	mem_deref(al->aubuf);
	mem_deref(al->lock);
}


static void print_stats(struct audio_loop *al)
{
	double rw_ratio = 0.0;
	double delay;
	const double scale = al->srate * al->ch;

	lock_read_get(al->lock);

	delay = (double)al->stats_src.n_samp - (double)al->stats_play.n_samp;

	rw_ratio = (double)al->stats_src.n_samp/(double)al->stats_play.n_samp;

	(void)re_fprintf(stdout, "\r%uHz %dch %s "
			 " n_read=%.3f n_write=%.3f rw_delay=%.3f [sec]"
			 " rw_ratio=%f"
			 ,
			 al->srate, al->ch, aufmt_name(al->fmt),
			 (double)al->stats_src.n_samp / scale,
			 (double)al->stats_play.n_samp / scale,
			 delay / scale, rw_ratio);

	(void)re_fprintf(stdout, "          \r");

	lock_rel(al->lock);

	fflush(stdout);
}


static void tmr_handler(void *arg)
{
	struct audio_loop *al = arg;

	tmr_start(&al->tmr, 100, tmr_handler, al);
	print_stats(al);
}


static void src_read_handler(struct auframe *af, void *arg)
{
	struct audio_loop *al = arg;
	struct stats *stats = &al->stats_src;
	int err;

	if (af->fmt != (int)al->fmt) {
		warning("auloop: format mismatch: exp=%d, actual=%d\n",
			al->fmt, af->fmt);
		return;
	}

	lock_write_get(al->lock);

	stats->n_samp   += af->sampc;
	stats->n_frames += 1;

	if (aubuf_cur_size(al->aubuf) >= al->aubuf_maxsz) {
		++al->aubuf_overrun;
	}

	lock_rel(al->lock);

	err = aubuf_write(al->aubuf, af->sampv, auframe_size(af));
	if (err) {
		warning("auloop: aubuf_write: %m\n", err);
	}
}


static void write_handler(void *sampv, size_t sampc, void *arg)
{
	struct audio_loop *al = arg;
	size_t num_bytes = sampc * aufmt_sample_size(al->fmt);
	struct stats *stats = &al->stats_play;

	lock_write_get(al->lock);

	stats->n_samp   += sampc;
	stats->n_frames += 1;

	if (stats->n_samp && aubuf_cur_size(al->aubuf) < num_bytes) {
		++al->aubuf_underrun;
	}

	lock_rel(al->lock);

	/* read from beginning */
	aubuf_read(al->aubuf, sampv, num_bytes);
}


static void error_handler(int err, const char *str, void *arg)
{
	(void)arg;
	warning("auloop: ausrc error: %m (%s)\n", err, str);
	gal = mem_deref(gal);
}


static int auloop_reset(struct audio_loop *al, uint32_t srate, uint8_t ch)
{
	struct auplay_prm auplay_prm;
	struct ausrc_prm ausrc_prm;
	const struct config *cfg = conf_config();
	size_t min_sz, sampsz;
	int err;

	if (!cfg)
		return ENOENT;

	if (cfg->audio.src_fmt != cfg->audio.play_fmt) {
		warning("auloop: ausrc_format and auplay_format"
			" must be the same\n");
		return EINVAL;
	}

	al->fmt = cfg->audio.src_fmt;

	/* audio player/source must be stopped first */
	al->auplay = mem_deref(al->auplay);
	al->ausrc  = mem_deref(al->ausrc);

	al->aubuf  = mem_deref(al->aubuf);

	al->srate = srate;
	al->ch    = ch;

	info("Audio-loop: %uHz, %dch, %s\n", al->srate, al->ch,
	     aufmt_name(al->fmt));

	sampsz = aufmt_sample_size(al->fmt);

	min_sz = sampsz * calc_nsamp(al->srate, al->ch, PTIME);
	al->aubuf_maxsz = sampsz * calc_nsamp(al->srate, al->ch, PTIME*5);

	err = aubuf_alloc(&al->aubuf, min_sz, al->aubuf_maxsz);
	if (err)
		return err;

	auplay_prm.srate      = al->srate;
	auplay_prm.ch         = al->ch;
	auplay_prm.ptime      = PTIME;
	auplay_prm.fmt        = al->fmt;
	err = auplay_alloc(&al->auplay, baresip_auplayl(),
			   cfg->audio.play_mod, &auplay_prm,
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
	ausrc_prm.fmt        = al->fmt;

	err = ausrc_alloc(&al->ausrc, baresip_ausrcl(),
			  NULL, cfg->audio.src_mod,
			  &ausrc_prm, cfg->audio.src_dev,
			  src_read_handler, error_handler, al);
	if (err) {
		warning("auloop: ausrc %s,%s failed: %m\n", cfg->audio.src_mod,
			cfg->audio.src_dev, err);
		return err;
	}

	return err;
}


static int audio_loop_alloc(struct audio_loop **alp,
			    uint32_t srate, uint8_t ch)
{
	struct audio_loop *al;
	int err;

	al = mem_zalloc(sizeof(*al), auloop_destructor);
	if (!al)
		return ENOMEM;

	err = lock_alloc(&al->lock);
	if (err)
		goto out;

	tmr_start(&al->tmr, 100, tmr_handler, al);

	err = auloop_reset(al, srate, ch);
	if (err)
		goto out;

	al->started = true;

 out:
	if (err)
		mem_deref(al);
	else
		*alp = al;

	return err;
}


/*
 * Start the audio loop (for testing)
 */
static int auloop_start(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct pl pl_srate, pl_ch;
	uint32_t srate, ch;
	int err;

	if (gal)
		return re_hprintf(pf, "audio-loop already running.\n");

	err = re_regex(carg->prm, str_len(carg->prm), "[0-9]+ [0-9]+",
		       &pl_srate, &pl_ch);
	if (err) {
		return re_hprintf(pf,
				  "Usage:"
				  " /auloop <samplerate> <channels>\n");
	}

	srate = pl_u32(&pl_srate);
	ch    = pl_u32(&pl_ch);
	if (!srate || !ch)
		return re_hprintf(pf, "invalid samplerate or channels\n");

	err = audio_loop_alloc(&gal, srate, (uint8_t)ch);
	if (err) {
		warning("auloop: alloc failed %m\n", err);
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
	{"auloop",     0,CMD_PRM, "Start audio-loop <srate ch>", auloop_start},
	{"auloop_stop",0,0,       "Stop audio-loop",             auloop_stop },
};


static int module_init(void)
{
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
