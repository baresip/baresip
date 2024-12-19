/**
 * @file capture.c  Pipewire sound driver - capture
 *
 * Copyright (C) 2023 Commend.com - c.spielberger@commend.com
 */

#include <string.h>
#include <errno.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include "pipewire.h"


struct ausrc_st {
	struct pw_stream *stream;

	struct ausrc_prm prm;
	ausrc_read_h *rh;
	struct spa_hook listener;

	size_t sampsz;
	uint64_t samps;

	void *arg;
};


static void on_process(void *arg);

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.process = on_process,
};


static void ausrc_destructor(void *arg)
{
	struct ausrc_st *st = arg;

	pw_thread_loop_lock (pw_loop_instance());
	st->rh = NULL;
	pw_stream_destroy(st->stream);
	pw_thread_loop_unlock(pw_loop_instance());
}


int pw_capture_alloc(struct ausrc_st **stp, const struct ausrc *as,
	struct ausrc_prm *prm, const char *dev, ausrc_read_h *rh,
	ausrc_error_h *errh, void *arg)
{
	struct ausrc_st *st;
	const struct spa_pod *params[1];
	uint8_t buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer,
							sizeof(buffer));
	const char name[] = "baresip-capture";
	char nlat[10];
	int err = 0;
	(void)errh;

	if (!stp || !as || !prm || !rh)
		return EINVAL;

	info ("pipewire: opening capture(%u Hz, %d channels,"
	      "device '%s')\n", prm->srate, prm->ch, dev);

	st = mem_zalloc(sizeof(*st), ausrc_destructor);
	if (!st)
		return ENOMEM;

	st->prm.srate = prm->srate;
	st->prm.ch    = prm->ch;
	st->prm.ptime = prm->ptime;
	st->prm.fmt   = prm->fmt;

	st->sampsz = aufmt_sample_size(prm->fmt);
	st->samps  = 0;

	st->rh   = rh;
	st->arg  = arg;
	re_snprintf(nlat, sizeof(nlat), "%u/1000", prm->ptime);

	pw_thread_loop_lock (pw_loop_instance());
	st->stream = pw_stream_new(pw_core_instance(), name,
			   pw_properties_new(
				     PW_KEY_MEDIA_TYPE, "Audio",
				     PW_KEY_MEDIA_CATEGORY, "Capture",
				     PW_KEY_MEDIA_ROLE, "Communication",
				     PW_KEY_TARGET_OBJECT, dev,
				     PW_KEY_NODE_LATENCY, nlat,
				     NULL));
	if (!st->stream) {
		err = errno;
		goto out;
	}

	pw_stream_add_listener(st->stream, &st->listener, &stream_events, st);
	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
					&SPA_AUDIO_INFO_RAW_INIT(
					.format = aufmt_to_pw_format(prm->fmt),
					.channels = prm->ch,
					.rate = prm->srate ));
	if (!params[0])
		goto out;

	err = pw_stream_connect(st->stream,
				PW_DIRECTION_INPUT,
				pw_device_id(dev),
				PW_STREAM_FLAG_AUTOCONNECT |
				PW_STREAM_FLAG_MAP_BUFFERS |
				PW_STREAM_FLAG_RT_PROCESS,
				params, 1);

	pw_thread_loop_unlock(pw_loop_instance());

	info ("pipewire: stream %s started (%m)\n", name, err);

  out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


/**
 * Pipewire process callback
 *
 * @param arg Argument (ausrc_st object)
 */
static void on_process(void *arg)
{
	struct ausrc_st *st = arg;
	struct pw_buffer *b;
	struct spa_buffer *buf;
	struct spa_data *d;
	struct auframe af;
	uint32_t offs;
	uint32_t size;

	void *sampv;
	size_t sampc;

	b = pw_stream_dequeue_buffer(st->stream);
	if (!b) {
		warning("pipewire: out of buffers (%m)\n", errno);
		return;
	}

	buf = b->buffer;
	d = &buf->datas[0];

	if (!d->data)
		return;

	offs  = SPA_MIN(d->chunk->offset, d->maxsize);
	size  = SPA_MIN(d->chunk->size, d->maxsize - offs);
	sampv = SPA_PTROFF(d->data, offs, void);
	sampc = size / st->sampsz;

	auframe_init(&af, st->prm.fmt, sampv, sampc,
		     st->prm.srate, st->prm.ch);

	af.timestamp = st->samps * AUDIO_TIMEBASE /
		       (st->prm.srate * st->prm.ch);
	st->samps += sampc;
	if (st->rh)
		st->rh(&af, st->arg);

	pw_stream_queue_buffer(st->stream, b);
}
