/**
 * @file ausine.c sine Audio Source
 *
 * Copyright (C) 2020 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <stdlib.h>
#include <math.h>
#define SCALE (32767)


/**
 * @defgroup ausine ausine
 *
 * Audio module generating sine wave as audio input
 *
 * Sample config:
 *
 \verbatim
  audio_source            ausine,400
 \endverbatim
 */

static const double PI = 3.14159265358979323846264338328;

struct ausrc_st {
	const struct ausrc *as;  /* base class */

	uint32_t ptime;
	size_t sampc;
	bool run;
	pthread_t thread;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
	int freq;
	double sec_offset;
};


static struct ausrc *ausrc;


static void destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}
}


static void *play_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct ausrc_st *st = arg;
	int16_t *sampv;
	double sample, rad_per_sec;
	double sec_per_frame = 1.0 / 48000;
	int inc;
	size_t frames;

	sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
	if (!sampv)
		return NULL;

	while (st->run) {

		struct auframe af = {
			.fmt   = AUFMT_S16LE,
			.sampv = sampv,
			.sampc = st->sampc,
			.timestamp = ts * 1000
		};

		sys_msleep(4);

		now = tmr_jiffies();

		if (ts > now)
			continue;

		inc = 0;
		rad_per_sec = st->freq * 2.0 * PI;
		frames = st->sampc / 2;

		for (size_t frame = 0; frame < frames; frame += 1) {
			sample = sin((st->sec_offset + frame * sec_per_frame)
					* rad_per_sec);
			sampv[inc] = (int16_t)(SCALE * 50 / 100.0f * sample);
			sampv[inc+1] = (int16_t)(SCALE * 50 / 100.0f * sample);
			inc += 2;
		}

		st->sec_offset = fmod(st->sec_offset + sec_per_frame * frames,
				1.0);

		st->rh(&af, st->arg);

		ts += st->ptime;
	}

	mem_deref(sampv);

	return NULL;
}


static int alloc_handler(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *dev,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;
	(void)ctx;

	if (!stp || !as || !prm || !rh || !dev)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("ausine: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	if (prm->srate != 48000) {
		warning("ausine: supports only 48kHz samplerate");
		return ENOTSUP;
	}

	if (prm->ch != 2) {
		warning("ausine: supports only 2 channels");
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->as   = as;
	st->rh   = rh;
	st->errh = errh;
	st->arg  = arg;
	st->sec_offset = 0.0;

	st->freq = atoi(dev);

	if (st->freq < 10 || st->freq > 20000)
	{
		warning("ausine: frequency must be between 10 and 20000 Hz\n");
		err = ENOTSUP;
		goto out;
	}

	info("ausine: %u Hz, %d channels, frequency %d Hz\n",
					prm->srate, prm->ch, st->freq);

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->ptime = prm->ptime;

	info("ausine: audio ptime=%u sampc=%zu\n",
	     st->ptime, st->sampc);

	st->run = true;
	err = pthread_create(&st->thread, NULL, play_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int module_init(void)
{
	return ausrc_register(&ausrc, baresip_ausrcl(),
			      "ausine", alloc_handler);
}


static int module_close(void)
{
	ausrc = mem_deref(ausrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(ausine) = {
	"ausine",
	"ausrc",
	module_init,
	module_close
};
