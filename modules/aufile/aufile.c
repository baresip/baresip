/**
 * @file aufile.c WAV Audio Source
 *
 * Copyright (C) 2015 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <pthread.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>


/**
 * @defgroup aufile aufile
 *
 * Audio module for using a WAV-file as audio input
 */


struct ausrc_st {
	const struct ausrc *as;  /* base class */
	struct tmr tmr;
	struct aufile *aufile;
	struct aubuf *aubuf;
	uint32_t ptime;
	size_t sampc;
	bool run;
	pthread_t thread;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
};


static struct ausrc *ausrc;


static void destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	tmr_cancel(&st->tmr);

	mem_deref(st->aufile);
	mem_deref(st->aubuf);
}


static void *play_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct ausrc_st *st = arg;
	int16_t *sampv;

	sampv = mem_alloc(st->sampc * 2, NULL);
	if (!sampv)
		return NULL;

	while (st->run) {

		sys_msleep(4);

		now = tmr_jiffies();

		if (ts > now)
			continue;

#if 1
		if (now > ts + 100) {
			debug("aufile: cpu lagging behind (%llu ms)\n",
			      now - ts);
		}
#endif

		aubuf_read_samp(st->aubuf, sampv, st->sampc);

		st->rh(sampv, st->sampc, st->arg);

		ts += st->ptime;
	}

	mem_deref(sampv);

	info("aufile: player thread exited\n");

	return NULL;
}


static void timeout(void *arg)
{
	struct ausrc_st *st = arg;

	tmr_start(&st->tmr, 1000, timeout, st);

	/* check if audio buffer is empty */
	if (aubuf_cur_size(st->aubuf) < (2 * st->sampc)) {

		info("aufile: end of file\n");

		/* error handler must be called from re_main thread */
		if (st->errh)
			st->errh(0, "end of file", st->arg);
	}
}


static int read_file(struct ausrc_st *st)
{
	struct mbuf *mb;
	int err;

	for (;;) {
		uint16_t *sampv;
		size_t i;

		mb = mbuf_alloc(4096);
		if (!mb)
			return ENOMEM;

		mb->end = mb->size;

		err = aufile_read(st->aufile, mb->buf, &mb->end);
		if (err)
			break;

		if (mb->end == 0) {
			info("aufile: end of file\n");
			break;
		}

		/* convert from Little-Endian to Native-Endian */
		sampv = (void *)mb->buf;
		for (i=0; i<mb->end/2; i++) {
			sampv[i] = sys_ltohs(sampv[i]);
		}

		aubuf_append(st->aubuf, mb);

		mb = mem_deref(mb);
	}

	info("aufile: loaded %zu bytes\n", aubuf_cur_size(st->aubuf));

	mem_deref(mb);
	return err;
}


static int alloc_handler(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *dev,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct aufile_prm fprm;
	int err;
	(void)ctx;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	if (prm->fmt != AUFMT_S16LE) {
		warning("aufile: unsupported sample format (%s)\n",
			aufmt_name(prm->fmt));
		return ENOTSUP;
	}

	info("aufile: loading input file '%s'\n", dev);

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->as   = as;
	st->rh   = rh;
	st->errh = errh;
	st->arg  = arg;

	err = aufile_open(&st->aufile, &fprm, dev, AUFILE_READ);
	if (err) {
		warning("aufile: failed to open file '%s' (%m)\n", dev, err);
		goto out;
	}

	info("aufile: %s: %u Hz, %d channels\n",
	     dev, fprm.srate, fprm.channels);

	if (fprm.srate != prm->srate) {
		warning("aufile: input file (%s) must have sample-rate"
			" %u Hz\n", dev, prm->srate);
		err = ENODEV;
		goto out;
	}
	if (fprm.channels != prm->ch) {
		warning("aufile: input file (%s) must have channels = %d\n",
			dev, prm->ch);
		err = ENODEV;
		goto out;
	}
	if (fprm.fmt != AUFMT_S16LE) {
		warning("aufile: input file must have format S16LE\n");
		err = ENODEV;
		goto out;
	}

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;

	st->ptime = prm->ptime;

	info("aufile: audio ptime=%u sampc=%zu aubuf=[%u:%u]\n",
	     st->ptime, st->sampc,
	     prm->srate * prm->ch * 2,
	     prm->srate * prm->ch * 40);

	/* 1 - inf seconds of audio */
	err = aubuf_alloc(&st->aubuf,
			  prm->srate * prm->ch * 2,
			  0);
	if (err)
		goto out;

	err = read_file(st);
	if (err)
		goto out;

	tmr_start(&st->tmr, 1000, timeout, st);

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
			      "aufile", alloc_handler);
}


static int module_close(void)
{
	ausrc = mem_deref(ausrc);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aufile) = {
	"aufile",
	"ausrc",
	module_init,
	module_close
};
