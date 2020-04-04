/**
 * @file rst/audio.c MP3/ICY HTTP Audio Source
 *
 * Copyright (C) 2011 Creytiv.com
 */
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <mpg123.h>
#include "rst.h"


struct ausrc_st {
	const struct ausrc *as;  /* pointer to base-class (inheritance) */
	pthread_t thread;
	struct rst *rst;
	mpg123_handle *mp3;
	struct aubuf *aubuf;
	ausrc_read_h *rh;
	ausrc_error_h *errh;
	void *arg;
	bool run;
	uint32_t ptime;
	size_t sampc;
	size_t sampsz;
	enum aufmt fmt;
};


static struct ausrc *ausrc;


static void destructor(void *arg)
{
	struct ausrc_st *st = arg;

	rst_set_audio(st->rst, NULL);
	mem_deref(st->rst);

	if (st->run) {
		st->run = false;
		pthread_join(st->thread, NULL);
	}

	if (st->mp3) {
		mpg123_close(st->mp3);
		mpg123_delete(st->mp3);
	}

	mem_deref(st->aubuf);
}


static void *play_thread(void *arg)
{
	uint64_t now, ts = tmr_jiffies();
	struct ausrc_st *st = arg;
	void *sampv;
	size_t num_bytes = st->sampc * st->sampsz;

	sampv = mem_alloc(num_bytes, NULL);
	if (!sampv)
		return NULL;

	while (st->run) {

		struct auframe af = {
			.fmt   = st->fmt,
			.sampv = sampv,
			.sampc = st->sampc,
			.timestamp = ts * 1000
		};

		sys_msleep(4);

		now = tmr_jiffies();

		if (ts > now)
			continue;
#if 1
		if (now > ts + 100) {
			debug("rst: cpu lagging behind (%u ms)\n",
			      now - ts);
		}
#endif

		aubuf_read(st->aubuf, sampv, num_bytes);

		st->rh(&af, st->arg);

		ts += st->ptime;
	}

	mem_deref(sampv);

	return NULL;
}


static inline int decode(struct ausrc_st *st)
{
	int err, ch, encoding;
	struct mbuf *mb;
	long srate;

	mb = mbuf_alloc(4096);
	if (!mb)
		return ENOMEM;

	err = mpg123_read(st->mp3, mb->buf, mb->size, &mb->end);

	switch (err) {

	case MPG123_NEW_FORMAT:
		mpg123_getformat(st->mp3, &srate, &ch, &encoding);
		info("rst: new format: %i hz, %i ch, encoding 0x%04x\n",
		     srate, ch, encoding);
		/*@fallthrough@*/

	case MPG123_OK:
	case MPG123_NEED_MORE:
		if (mb->end == 0)
			break;
		aubuf_append(st->aubuf, mb);
		break;

	default:
		warning("rst: mpg123_read error: %s\n",
			mpg123_plain_strerror(err));
		break;
	}

	mem_deref(mb);

	return err;
}


void rst_audio_feed(struct ausrc_st *st, const uint8_t *buf, size_t sz)
{
	int err;

	if (!st)
		return;

	err = mpg123_feed(st->mp3, buf, sz);
	if (err)
		return;

	while (MPG123_OK == decode(st))
		;
}


static int aufmt_to_encoding(enum aufmt fmt)
{
	switch (fmt) {

	case AUFMT_S16LE:   return MPG123_ENC_SIGNED_16;
	case AUFMT_FLOAT:   return MPG123_ENC_FLOAT_32;
	case AUFMT_S24_3LE: return MPG123_ENC_SIGNED_24;  /* NOTE: endian */
	default: return 0;
	}
}


static int alloc_handler(struct ausrc_st **stp, const struct ausrc *as,
			 struct media_ctx **ctx,
			 struct ausrc_prm *prm, const char *dev,
			 ausrc_read_h *rh, ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	int encoding;
	int err;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	encoding = aufmt_to_encoding(prm->fmt);
	if (!encoding)
		return ENOTSUP;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return ENOMEM;

	st->as   = as;
	st->rh   = rh;
	st->errh = errh;
	st->arg  = arg;

	st->mp3 = mpg123_new(NULL, &err);
	if (!st->mp3) {
		err = ENODEV;
		goto out;
	}

	err = mpg123_open_feed(st->mp3);
	if (err != MPG123_OK) {
		warning("rst: mpg123_open_feed: %s\n",
			mpg123_strerror(st->mp3));
		err = ENODEV;
		goto out;
	}

	/* Set wanted output format */
	mpg123_format_none(st->mp3);
	err = mpg123_format(st->mp3, prm->srate, prm->ch, encoding);
	if (err != MPG123_OK) {
		warning("rst: mpg123_format: %s\n",
			mpg123_strerror(st->mp3));
		err = ENOTSUP;
		goto out;
	}

	mpg123_volume(st->mp3, 0.3);

	st->sampc = prm->srate * prm->ch * prm->ptime / 1000;
	st->sampsz = aufmt_sample_size(prm->fmt);
	st->fmt = prm->fmt;

	st->ptime = prm->ptime;

	info("rst: audio ptime=%u sampc=%zu aubuf=[%u:%u]\n",
	     st->ptime, st->sampc,
	     prm->srate * prm->ch * 2,
	     prm->srate * prm->ch * 40);

	/* 1 - 20 seconds of audio */
	err = aubuf_alloc(&st->aubuf,
			  prm->srate * prm->ch * st->sampsz,
			  prm->srate * prm->ch * st->sampsz * 20);
	if (err)
		goto out;

	if (ctx && *ctx && (*ctx)->id && !strcmp((*ctx)->id, "rst")) {
		st->rst = mem_ref(*ctx);
	}
	else {
		err = rst_alloc(&st->rst, dev);
		if (err)
			goto out;

		if (ctx)
			*ctx = (struct media_ctx *)st->rst;
	}

	rst_set_audio(st->rst, st);

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


int rst_audio_init(void)
{
	int err;

	err = mpg123_init();
	if (err != MPG123_OK) {
		warning("rst: mpg123_init: %s\n", mpg123_plain_strerror(err));
		return ENODEV;
	}

	return ausrc_register(&ausrc, baresip_ausrcl(), "rst", alloc_handler);
}


void rst_audio_close(void)
{
	ausrc = mem_deref(ausrc);

	mpg123_exit();
}
