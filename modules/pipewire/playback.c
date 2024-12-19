/**
 * @file playback.c  Pipewire sound driver - playback
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

struct auplay_st {
	struct pw_stream *stream;

	struct auplay_prm prm;
	auplay_write_h *wh;
	struct spa_hook listener;

	size_t sampc;
	size_t nbytes;
	int32_t stride;

	void *arg;
};

static void on_process(void *arg);

static const struct pw_stream_events stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.process = on_process,
};


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	pw_thread_loop_lock (pw_loop_instance());
	st->wh = NULL;
	pw_stream_destroy(st->stream);
	pw_thread_loop_unlock(pw_loop_instance());
}


int pw_playback_alloc(struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *dev, auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	const struct spa_pod *params[1];
	uint8_t buffer[1024];
	struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer,
							sizeof(buffer));
	const char name[] = "baresip-playback";
	size_t sampsz;
	int err = 0;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	info ("pipewire: opening playback (%u Hz, %d channels, device %s, "
		"ptime %u)\n", prm->srate, prm->ch, dev, prm->ptime);

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	st->prm.srate = prm->srate;
	st->prm.ch    = prm->ch;
	st->prm.ptime = prm->ptime;
	st->prm.fmt   = prm->fmt;

	sampsz = aufmt_sample_size(prm->fmt);
	st->sampc  = st->prm.ptime * st->prm.ch * st->prm.srate / 1000;
	st->nbytes = st->sampc * sampsz;
	st->stride = (int32_t)sampsz * prm->ch;

	st->wh  = wh;
	st->arg = arg;

	pw_thread_loop_lock (pw_loop_instance());
	st->stream = pw_stream_new(pw_core_instance(), name,
			   pw_properties_new(
				     PW_KEY_MEDIA_TYPE, "Audio",
				     PW_KEY_MEDIA_CATEGORY, "Playback",
				     PW_KEY_MEDIA_ROLE, "Communication",
				     PW_KEY_TARGET_OBJECT, dev,
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
				PW_DIRECTION_OUTPUT,
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
 * @param arg Argument (auplay_st object)
 */
static void on_process(void *arg)
{
	struct auplay_st *st = arg;
	struct pw_buffer *b;
	struct spa_buffer *buf;
	struct spa_data *d;
	struct auframe af;
	void *sampv;

	b = pw_stream_dequeue_buffer(st->stream);
	if (!b) {
		warning("pipewire: out of buffers (%m)\n", errno);
		return;
	}

	buf = b->buffer;
	d = &buf->datas[0];

	if (!d->data)
		return;

	sampv = d->data;
	if (d->maxsize < st->nbytes) {
		warning("pipewire: buffer to small\n");
		return;
	}

	auframe_init(&af, st->prm.fmt, sampv, st->sampc,
		     st->prm.srate, st->prm.ch);

	if (st->wh)
		st->wh(&af, st->arg);

	d->chunk->offset = 0;
	d->chunk->stride = st->stride;
	d->chunk->size   = (uint32_t)auframe_size(&af);

	pw_stream_queue_buffer(st->stream, b);
}
