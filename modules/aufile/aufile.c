/**
 * @file aufile.c WAV Audio Source
 *
 * Copyright (C) 2015 Alfred E. Heggestad
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <pthread.h>
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
	struct ausrc_prm *prm;          /**< Audio src parameter             */
	uint32_t ptime;
	size_t sampc;
	bool run;
	pthread_t thread;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
};


static struct ausrc *ausrc;
static struct auplay *auplay;


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
	uint32_t ms = 4;

	if (!st->ptime)
		ms = 0;

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

		sys_msleep(ms);

		now = tmr_jiffies();

		if (ts > now)
			continue;

		aubuf_read_samp(st->aubuf, sampv, st->sampc);

		st->rh(&af, st->arg);

		ts += st->ptime;

		if (aubuf_cur_size(st->aubuf) == 0)
			st->run = false;
	}

	mem_deref(sampv);

	return NULL;
}


static void timeout(void *arg)
{
	struct ausrc_st *st = arg;
	tmr_start(&st->tmr, st->ptime ? st->ptime : 40, timeout, st);

	/* check if audio buffer is empty */
	if (!st->run) {
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

			aubuf_append(st->aubuf, mb);
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
			aubuf_append(st->aubuf, mb2);
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


static int alloc_handler(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *dev,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	struct aufile_prm fprm;
	uint32_t   ptime;
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

	st->rh    = rh;
	st->errh  = errh;
	st->arg   = arg;
	st->prm   = prm;
	st->ptime = prm->ptime;

	ptime = st->ptime;
	if (!ptime)
		ptime = 40;

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

	st->fmt    = fprm.fmt;
	st->sampc  = prm->srate * prm->ch * ptime / 1000;

	info("aufile: audio ptime=%u sampc=%zu\n", st->ptime, st->sampc);

	/* 1 - inf seconds of audio */
	err = aubuf_alloc(&st->aubuf,
			  st->sampc * 2,
			  0);
	if (err)
		goto out;

	err = read_file(st);
	if (err)
		goto out;

	tmr_start(&st->tmr, ptime, timeout, st);

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
	int err;
	err  = ausrc_register(&ausrc, baresip_ausrcl(),
			      "aufile", alloc_handler);
	err |= auplay_register(&auplay, baresip_auplayl(),
			       "aufile", play_alloc);
	return err;
}


static int module_close(void)
{
	ausrc = mem_deref(ausrc);
	auplay = mem_deref(auplay);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(aufile) = {
	"aufile",
	"ausrc",
	module_init,
	module_close
};
