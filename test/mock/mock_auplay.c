/**
 * @file mock/mock_auplay.c Mock audio player
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "../test.h"


struct auplay_st {
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


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	tmr_cancel(&st->tmr);
	mem_deref(st->sampv);
}


static void tmr_handler(void *arg)
{
	struct auplay_st *st = arg;
	struct auframe af;

	tmr_start(&st->tmr, st->prm.ptime, tmr_handler, st);

	auframe_init(&af, st->prm.fmt, st->sampv, st->sampc, st->prm.srate,
		     st->prm.ch);

	if (st->wh)
		st->wh(&af, st->arg);

	/* feed the audio-samples back to the test */
	if (mock.sampleh)
		mock.sampleh(st->sampv, st->sampc, mock.arg);
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


int mock_auplay_register(struct auplay **auplayp, struct list *auplayl,
			 mock_sample_h *sampleh, void *arg)
{
	mock.sampleh = sampleh;
	mock.arg = arg;

	return auplay_register(auplayp, auplayl,
			      "mock-auplay", mock_auplay_alloc);
}
