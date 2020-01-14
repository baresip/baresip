/**
 * @file i2s_src.c freeRTOS I2S audio driver module - recorder
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


struct ausrc_st {
	const struct ausrc *as;  /* pointer to base-class (inheritance) */
	pthread_t thread;
	bool run;
	void *sampv;
	size_t sampc;
	ausrc_read_h *rh;
	void *arg;
	struct ausrc_prm prm;

	uint32_t *pcm;
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	/* Wait for termination of other thread */
	if (st->run) {
		info("i2s: stopping recording thread\n");
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	mem_deref(st->sampv);
	mem_deref(st->pcm);
}


/**
 * Converts the pcm 32 bit I2S values to baresip 16-bit samples. A reasonable
 * volume for the microphone is achieved by right shifting.
 * @param st The ausrc_st.
 * @param i  Offset for st->sampv.
 * @param n  The number of samples that should be converted.
 */
static void convert_pcm(struct ausrc_st *st, size_t i, size_t n)
{
	uint32_t j;
	uint16_t *sampv = st->sampv;
	for (j = 0; j < n; j++) {
		uint32_t v = st->pcm[j];
		uint16_t *o = sampv + i + j;
		*o = v >> 15;
		/* if negative fill with ff */
		if (v & 0x80000000)
			*o |= 0xfffe0000;
	}
}


static void *read_thread(void *arg)
{
	struct ausrc_st *st = arg;

	while (st->run) {
		size_t i;

		for (i = 0; i + DMA_SIZE / 4 <= st->sampc;) {
			size_t n = 0;
			if (i2s_read(I2S_PORT, st->pcm, DMA_SIZE, &n,
						portMAX_DELAY) != ESP_OK)
				break;

			if (n == 0)
				break;

			convert_pcm(st, i, n / 4);
			i += (n / 4);
		}

		st->rh(st->sampv, st->sampc, st->arg);
	}

	i2s_stop_bus(I2O_RECO);
	info("i2s: stopped ausrc thread\n");

	return NULL;
}


int i2s_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
			     struct media_ctx **ctx,
			     struct ausrc_prm *prm, const char *device,
			     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	size_t sampc;
	int err;

	(void) ctx;
	(void) device;
	(void) errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	if (prm->fmt!=AUFMT_S16LE) {
		warning("i2s: unsupported sample format %s\n",
				aufmt_name(prm->fmt));
		return EINVAL;
	}

	sampc = prm->srate * prm->ch * prm->ptime / 1000;
	if (sampc % (DMA_SIZE / 4)) {
		warning("i2s: sampc=%d has to be divisible by DMA_SIZE/4\n",
				sampc);
		return EINVAL;
	}

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->prm = *prm;
	st->as  = as;
	st->rh  = rh;
	st->arg = arg;

	st->sampc = sampc;
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

	err = i2s_start_bus(st->prm.srate, I2O_RECO, st->prm.ch);
	if (err)
		goto out;

	st->run = true;
	info("%s starting src thread\n", __func__);
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err) {
		st->run = false;
		goto out;
	}

	debug("i2s: recording\n");

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}
