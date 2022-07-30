/**
 * @file ausine.c sine Audio Source
 *
 * Copyright (C) 2020 Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
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
  audio_source            ausine,400,stereo_left
  audio_source            ausine,400,stereo_right
 \endverbatim
 */

static const double PI = 3.14159265358979323846264338328;

enum channels {
	STEREO,       /* Default 2ch (detected) */
	STEREO_LEFT,  /* Stereo left channel only (manual) */
	STEREO_RIGHT, /* Stereo right channel only (manual) */
	MONO          /* Fallback 1ch (detected) */
};

struct ausrc_st {
	uint32_t ptime;
	size_t sampc;
	mtx_t mutex;
	bool run;
	thrd_t thread;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
	int freq;
	double sec_offset;
	enum channels ch;
	struct ausrc_prm prm;
};


static struct ausrc *ausrc;


static void destructor(void *arg)
{
	struct ausrc_st *st = arg;
	bool run;

	mtx_lock(&st->mutex);
	run = st->run;
	mtx_unlock(&st->mutex);

	if (run) {

		mtx_lock(&st->mutex);
		st->run = false;
		mtx_unlock(&st->mutex);

		thrd_join(st->thread, NULL);
	}

	mtx_destroy(&st->mutex);
}


static int play_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct ausrc_st *st = arg;
	int16_t *sampv;
	double sample, rad_per_sec;
	double sec_per_frame = 1.0 / (double)st->prm.srate;
	int inc;
	size_t frames;
	int16_t f;

	sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
	if (!sampv)
		return ENOMEM;

	while (1) {
		struct auframe af;
		size_t frame;
		bool run;

		mtx_lock(&st->mutex);
		run = st->run;
		mtx_unlock(&st->mutex);

		if (!run)
			break;

		auframe_init(&af, AUFMT_S16LE, sampv, st->sampc, st->prm.srate,
		             st->prm.ch);
		af.timestamp = ts * 1000;

		sys_msleep(4);

		now = tmr_jiffies();

		if (ts > now)
			continue;

		inc = 0;
		rad_per_sec = st->freq * 2.0 * PI;

		if (st->ch == MONO) {
			frames = st->sampc;
		}
		else {
			frames = st->sampc / 2;
		}

		for (frame = 0; frame < frames; frame += 1) {
			sample = sin((st->sec_offset + frame * sec_per_frame)
					* rad_per_sec);

			f = (int16_t)(SCALE * 50 / 100.0f * sample);

			if (st->ch == STEREO) {
				sampv[inc] = f;
				sampv[inc + 1] = f;
				inc += 2;
			}
			if (st->ch == STEREO_LEFT) {
				sampv[inc] = f;
				sampv[inc + 1] = 0;
				inc += 2;
			}
			if (st->ch == STEREO_RIGHT) {
				sampv[inc] = 0;
				sampv[inc + 1] = f;
				inc += 2;
			}
			if (st->ch == MONO) {
				sampv[inc] = f;
				inc += 1;
			}
		}

		st->sec_offset = fmod(st->sec_offset + sec_per_frame * frames,
				1.0);

		st->rh(&af, st->arg);

		ts += st->ptime;
	}

	mem_deref(sampv);

	return 0;
}


static enum channels stereo_conf(const char *dev)
{
	struct pl r, pl1, pl2 = pl_null;
	int err;

	pl_set_str(&r, dev);

	err = re_regex(r.p, r.l, "[^,]+,[~]*", &pl1, &pl2);
	if (err)
		return STEREO;

	if (pl_isset(&pl2)) {
		if (!pl_strcmp(&pl2, "stereo_left"))
			return STEREO_LEFT;
		if (!pl_strcmp(&pl2, "stereo_right"))
			return STEREO_RIGHT;
	}
	return STEREO;
}


static int alloc_handler(struct ausrc_st **stp, const struct ausrc *as,
			 struct ausrc_prm *prm, const char *dev,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int err;

	if (!stp || !as || !prm || !rh || !dev)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("ausine: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->rh   = rh;
	st->errh = errh;
	st->arg  = arg;
	st->sec_offset = 0.0;
	st->prm = *prm;

	st->freq = str_isset(dev) ? atoi(dev) : 440;

	st->ch = stereo_conf(dev);
	if (prm->ch == 1) {
		st->ch = MONO;
	}

	if (st->freq < 10 || st->freq > 20000) {
		warning("ausine: frequency must be between 10 and 20000 Hz\n");
		err = ENOTSUP;
		goto out;
	}

	info("ausine: %u Hz, %d channels, frequency %d Hz CH_MODE: %d\n",
					prm->srate, prm->ch, st->freq, st->ch);

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->ptime = prm->ptime;

	info("ausine: audio ptime=%u sampc=%zu\n",
	     st->ptime, st->sampc);

	err = mtx_init(&st->mutex, mtx_plain);
	if (err != thrd_success) {
		err = ENOMEM;
		goto out;
	}

	st->run = true;
	err = thread_create_name(&st->thread, "ausine", play_thread, st);
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
