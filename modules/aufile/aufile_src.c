/**
 * @file aufile_src.c WAV Audio Source
 *
 * Copyright (C) 2022 commend.com - Christian Spielberger
 */
#include <re_atomic.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "aufile.h"


/**
 * @defgroup aufile aufile
 *
 * Audio module for using a WAV-file as audio input
 *
 * Sample config:
 *
 \verbatim
  audio_source            aufile,/tmp/test.wav
 \endverbatim
 */


struct ausrc_st {
	struct tmr tmr;
	struct aufile *aufile;
	struct aubuf *aubuf;
	enum aufmt fmt;                 /**< Wav file sample format          */
	struct ausrc_prm prm;           /**< Audio src parameter             */
	uint32_t ptime;
	size_t sampc;
	RE_ATOMIC bool run;
	bool started;
	thrd_t thread;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
};


static void destructor(void *arg)
{
	struct ausrc_st *st = arg;

	if (st->started) {
		re_atomic_rlx_set(&st->run, false);
		thrd_join(st->thread, NULL);
	}

	tmr_cancel(&st->tmr);

	mem_deref(st->aufile);
	mem_deref(st->aubuf);
}


static int src_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct ausrc_st *st = arg;
	int16_t *sampv;
	uint32_t ms = 4;

	sampv = mem_alloc(st->sampc * sizeof(int16_t), NULL);
	if (!sampv)
		return ENOMEM;

	while (re_atomic_rlx(&st->run)) {
		struct auframe af;

		sys_msleep(ms);
		now = tmr_jiffies();
		if (ts > now)
			continue;

		auframe_init(&af, AUFMT_S16LE, sampv, st->sampc,
		             st->prm.srate, st->prm.ch);

		aubuf_read_auframe(st->aubuf, &af);

		st->rh(&af, st->arg);

		ts += st->ptime;

		if (aubuf_cur_size(st->aubuf) == 0)
			break;
	}

	mem_deref(sampv);
	re_atomic_rlx_set(&st->run, false);

	return 0;
}


static void timeout(void *arg)
{
	struct ausrc_st *st = arg;
	tmr_start(&st->tmr, st->ptime ? st->ptime : 40, timeout, st);

	/* check if audio buffer is empty */
	if (!re_atomic_rlx(&st->run)) {
		tmr_cancel(&st->tmr);

		info("aufile: end of file\n");

		/* error handler must be called from re_main thread */
		if (st->errh)
			st->errh(0, "end of file", st->arg);
	}
}


static int read_file(struct ausrc_st *st)
{
	struct mbuf *mb = NULL;
	int err;
	size_t n;
	struct mbuf *mb2 = NULL;
	struct auframe af;

	auframe_init(&af, AUFMT_S16LE, NULL, 0, st->prm.srate, st->prm.ch);

	for (;;) {
		uint16_t *sampv;
		uint8_t *p;
		size_t i;

		mem_deref(mb);
		mb = mbuf_alloc(4096);
		if (!mb)
			return ENOMEM;

		mb->end = mb->size;

		err = aufile_read(st->aufile, mb->buf, &mb->end);
		if (err)
			break;

		if (mb->end == 0) {
			info("aufile: read end of file\n");
			break;
		}

		/* convert from Little-Endian to Native-Endian */
		n = mb->end;
		sampv = (void *)mb->buf;
		p     = (void *)mb->buf;

		switch (st->fmt) {
		case AUFMT_S16LE:
			/* convert from Little-Endian to Native-Endian */
			for (i = 0; i < n/2; i++)
				sampv[i] = sys_ltohs(sampv[i]);

			aubuf_append_auframe(st->aubuf, mb, &af);
			break;
		case AUFMT_PCMA:
		case AUFMT_PCMU:
			mb2 = mbuf_alloc(2 * n);
			for (i = 0; i < n; i++) {
				err |= mbuf_write_u16(mb2,
					   st->fmt == AUFMT_PCMA ?
					   (uint16_t) g711_alaw2pcm(p[i]) :
					   (uint16_t) g711_ulaw2pcm(p[i]) );
			}

			mbuf_set_pos(mb2, 0);
			aubuf_append_auframe(st->aubuf, mb2, &af);
			mem_deref(mb2);
			break;

		default:
			err = ENOSYS;
			break;
		}

		if (err)
			break;
	}

	info("aufile: loaded %zu bytes\n", aubuf_cur_size(st->aubuf));
	mem_deref(mb);
	return err;
}


int aufile_src_alloc(struct ausrc_st **stp, const struct ausrc *as,
		     struct ausrc_prm *prm, const char *dev,
		     ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct aufile_prm fprm;
	int err;

	if (!stp || !as || !prm)
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

	st->rh    = rh;
	st->errh  = errh;
	st->arg   = arg;
	st->ptime = prm->ptime;
	if (!st->ptime)
		st->ptime = 20;

	err = aufile_open(&st->aufile, &fprm, dev, AUFILE_READ);
	if (err) {
		warning("aufile: failed to open file '%s' (%m)\n", dev, err);
		goto out;
	}

	info("aufile: %s: %u Hz, %d channels, %s\n",
	     dev, fprm.srate, fprm.channels, aufmt_name(fprm.fmt));

	/* return wav format to caller */
	prm->srate = fprm.srate;
	prm->ch    = fprm.channels;

	if (!rh)
		goto out;

	st->prm   = *prm;

	st->fmt    = fprm.fmt;
	st->sampc  = prm->srate * prm->ch * st->ptime / 1000;

	info("aufile: audio ptime=%u sampc=%zu\n", st->ptime, st->sampc);

	/* 1 - inf seconds of audio */
	err = aubuf_alloc(&st->aubuf, 0, 0);
	if (err)
		goto out;

	err = read_file(st);
	if (err)
		goto out;

	tmr_start(&st->tmr, st->ptime, timeout, st);

	re_atomic_rlx_set(&st->run, true);
	st->started = true;
	err = thread_create_name(&st->thread, "aufile_src", src_thread, st);
	if (err) {
		st->started = false;
		re_atomic_rlx_set(&st->run, false);
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


int aufile_info_handler(const struct ausrc *as,
			struct ausrc_prm *prm, const char *dev)
{
	int err;
	(void)as;

	if (!prm || !str_isset(dev))
		return EINVAL;

	struct aufile *aufile;
	struct aufile_prm fprm;
	err = aufile_open(&aufile, &fprm, dev, AUFILE_READ);
	if (err) {
		warning("aufile: failed to open file '%s' (%m)\n", dev, err);
		return err;
	}

	prm->srate    = fprm.srate;
	prm->ch       = fprm.channels;
	prm->fmt      = fprm.fmt;
	prm->duration = aufile_get_length(aufile, &fprm);

	mem_deref(aufile);
	return err;
}
