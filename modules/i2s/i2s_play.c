/**
 * @file i2s_play.c freeRTOS I2S audio driver module - player
 *
 * Copyright (C) 2019 cspiel.at
 */
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "i2s.h"


struct auplay_st {
	const struct auplay *ap;  /* pointer to base-class (inheritance) */
	pthread_t thread;
	bool run;
	void *sampv;
	size_t sampc;
	auplay_write_h *wh;
	void *arg;
	struct auplay_prm prm;

	uint32_t *pcm;
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		info("i2s: stopping playback thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->pcm);
}


/**
 * Converts samples from int16_t to pcm 32 bit values ready for I2S bus. A
 * reasonable volume of the playback is achieved by left-shifting.
 * @param st The auplay_st.
 * @param i  Offset for st->sampv.
 * @param n  Number of samples that should be converted.
 */
static void convert_sampv(struct auplay_st *st, size_t i, size_t n)
{
	uint32_t j;
	int16_t *sampv = st->sampv;
	for (j = 0; j < n; j++) {
		uint32_t v = sampv[i+j];
		st->pcm[j] = v << 17;
	}
}

static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;

	i2s_set_clk(I2S_PORT, st->prm.srate, 32, st->prm.ch);
	while (st->run) {
		size_t i;

		st->wh(st->sampv, st->sampc, st->arg);
		for (i = 0; i + DMA_SIZE / 4 <= st->sampc;) {
			size_t n;
			convert_sampv(st, i, DMA_SIZE / 4);

			if (i2s_write(I2S_PORT, (const uint8_t*) st->pcm,
					DMA_SIZE, &n, portMAX_DELAY) != ESP_OK)
				break;

			if (n != DMA_SIZE)
				warning("i2s: written %lu bytes but expected"
					" %lu.", n, DMA_SIZE);

			if (n == 0)
				break;

			i += (n / 4);
		}
	}

	i2s_stop_bus(I2O_PLAY);
	info("i2s: stopped auplay thread\n");

	return NULL;
}


int i2s_play_alloc(struct auplay_st **stp, const struct auplay *ap,
		    struct auplay_prm *prm, const char *device,
		    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err;

	(void) device;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	if (prm->fmt!=AUFMT_S16LE) {
		warning("i2s: unsupported sample format %s\n",
			aufmt_name(prm->fmt));
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->ap  = ap;
	st->wh  = wh;
	st->arg = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampv = mem_zalloc(aufmt_sample_size(st->prm.fmt)*st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}
	st->pcm = mem_zalloc(DMA_SIZE, NULL);
	if (!st->pcm) {
		err = ENOMEM;
		goto out;
	}

	err = i2s_start_bus(st->prm.srate, I2O_PLAY, st->prm.ch);
	if (err)
		goto out;

	st->run = true;
	info("%s starting play thread\n", __func__);
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("i2s: playback started\n");

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
