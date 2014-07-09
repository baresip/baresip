/**
 * @file sndio.c  SndIO sound driver
 *
 * Copyright (C) 2014 Creytiv.com
 */
#include <stdlib.h>
#include <string.h>
#include <sndio.h>
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup sndio sndio
 *
 * This module implements audio driver for OpenBSD sndio
 */


struct ausrc_st {
	struct ausrc *as;
	struct sio_hdl *hdl;
	pthread_t thread;
	int16_t *sampv;
	size_t sampc;
	int run;
	ausrc_read_h *rh;
	void *arg;
};

struct auplay_st {
	struct auplay *ap;
	struct sio_hdl *hdl;
	pthread_t thread;
	int16_t *sampv;
	size_t sampc;
	int run;
	auplay_write_h *wh;
	void *arg;
};

static struct ausrc *ausrc;
static struct auplay *auplay;


static struct sio_par *sndio_initpar(void *arg)
{
	struct sio_par *par;
	struct auplay_prm *prm = arg;

	if ((par = malloc(sizeof(struct sio_par))) == NULL)
		return NULL;

	sio_initpar(par);

	/* sndio doesn't support a-low and u-low */
	par->bits = 16;
	par->bps  = SIO_BPS(par->bits);
	par->sig  = 1;
	par->le   = 1;

	par->rchan = prm->ch;
	par->pchan = prm->ch;
	par->rate = prm->srate;

	return par;
}


static void *read_thread(void *arg)
{
	struct ausrc_st *st = arg;

	if (!sio_start(st->hdl)) {
		warning("sndio: could not start record\n");
		goto out;
	}

	while (st->run) {
		size_t n = sio_read(st->hdl, st->sampv, st->sampc*2);
		st->rh(st->sampv, n/2, st->arg);
	}

 out:
	return NULL;
}


static void *write_thread(void *arg)
{
	struct auplay_st *st = arg;

	if (!sio_start(st->hdl)) {
		warning("sndio: could not start playback\n");
		goto out;
	}

	while (st->run) {
		st->wh(st->sampv, st->sampc, st->arg);
		sio_write(st->hdl, st->sampv, st->sampc*2);
	}

 out:
	return NULL;
}


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->run) {
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	if (st->hdl)
		sio_close(st->hdl);

	mem_deref(st->sampv);
	mem_deref(st->as);
}


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	if (st->run) {
		st->run = false;
		(void)pthread_join(st->thread, NULL);
	}

	if (st->hdl)
		sio_close(st->hdl);

	mem_deref(st->sampv);
	mem_deref(st->ap);
}


static int src_alloc(struct ausrc_st **stp, struct ausrc *as,
		     struct media_ctx **ctx,
		     struct ausrc_prm *prm, const char *device,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct sio_par *par;
	int err;
	const char *name;

	(void)ctx;
	(void)errh;

	if (!stp || !as || !prm)
		return EINVAL;

	name = (str_isset(device)) ? device : SIO_DEVANY;

	if ((st = mem_zalloc(sizeof(*st), ausrc_destructor)) == NULL)
		return ENOMEM;

	st->as  = mem_ref(as);
	st->rh  = rh;
	st->arg = arg;
	st->hdl = sio_open(name, SIO_REC, 0);

	if (!st->hdl) {
		warning("sndio: could not open ausrc device '%s'\n", name);
		err = EINVAL;
		goto out;
	}

	par = sndio_initpar(prm);
	if (!par) {
		err = ENOMEM;
		goto out;
	}

	if (!sio_setpar(st->hdl, par)) {
		free(par);
		err = EINVAL;
		goto out;
	}

	if (!sio_getpar(st->hdl, par)) {
		free(par);
		err = EINVAL;
		goto out;
	}

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->sampv = mem_alloc(2 * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	free(par);

	st->run = true;
	err = pthread_create(&st->thread, NULL, read_thread, st);
	if (err)
		st->run = false;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int play_alloc(struct auplay_st **stp, struct auplay *ap,
		      struct auplay_prm *prm, const char *device,
		      auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	struct sio_par *par;
	int err;
	const char *name;

	if (!stp || !ap || !prm)
		return EINVAL;

	name = (str_isset(device)) ? device : SIO_DEVANY;

	if ((st = mem_zalloc(sizeof(*st), auplay_destructor)) == NULL)
		return ENOMEM;

	st->ap  = mem_ref(ap);
	st->wh  = wh;
	st->arg = arg;
	st->hdl = sio_open(name, SIO_PLAY, 0);

	if (!st->hdl) {
		warning("sndio: could not open auplay device '%s'\n", name);
		err = EINVAL;
		goto out;
	}

	par = sndio_initpar(prm);
	if (!par) {
		err = ENOMEM;
		goto out;
	}

	if (!sio_setpar(st->hdl, par)) {
		free(par);
		err = EINVAL;
		goto out;
	}

	if (!sio_getpar(st->hdl, par)) {
		free(par);
		err = EINVAL;
		goto out;
	}

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->sampv = mem_alloc(2 * st->sampc, NULL);
	if (!st->sampv) {
		err = ENOMEM;
		goto out;
	}

	free(par);

	st->run = true;
	err = pthread_create(&st->thread, NULL, write_thread, st);
	if (err)
		st->run = false;

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int sndio_init(void)
{
	int err = 0;

	err |= ausrc_register(&ausrc, "sndio", src_alloc);
	err |= auplay_register(&auplay, "sndio", play_alloc);

	return err;
}


static int sndio_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(sndio) = {
	"sndio",
	"sound",
	sndio_init,
	sndio_close
};
