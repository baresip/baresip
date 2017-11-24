/**
 * @file mock/mock_auplay.c Mock audio player
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "../test.h"


struct auplay_st {
	const struct auplay *ap;      /* inheritance */

	struct tmr tmr;
	struct auplay_prm prm;
	void *sampv;
	size_t sampc;
	auplay_write_h *wh;
	void *arg;
};


static struct {
	mock_sample_h *sampleh;
	void *arg;
} mock;


static void tmr_handler(void *arg)
{
	struct auplay_st *st = arg;

	tmr_start(&st->tmr, st->prm.ptime, tmr_handler, st);

	if (st->wh)
		st->wh(st->sampv, st->sampc, st->arg);

	/* feed the audio-samples back to the test */
	if (mock.sampleh)
		mock.sampleh(st->sampv, st->sampc, mock.arg);
}


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	tmr_cancel(&st->tmr);
	mem_deref(st->sampv);
}


static int mock_auplay_alloc(struct auplay_st **stp, const struct auplay *ap,
			    struct auplay_prm *prm, const char *device,
			    auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	int err = 0;
	(void)device;

	if (!stp || !ap || !prm)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->ap   = ap;
	st->prm  = *prm;
	st->wh   = wh;
	st->arg  = arg;

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->sampv = mem_zalloc(aufmt_sample_size(prm->fmt) * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	tmr_start(&st->tmr, 0, tmr_handler, st);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


int mock_auplay_register(struct auplay **auplayp,
			 mock_sample_h *sampleh, void *arg)
{
	mock.sampleh = sampleh;
	mock.arg = arg;

	return auplay_register(auplayp, baresip_auplayl(),
			      "mock-auplay", mock_auplay_alloc);
}
