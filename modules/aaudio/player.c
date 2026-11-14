/**
 * @file aaudio/player.c  AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 * Copyright (C) 2024 Sebastian Reimers
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <re.h>
#include <rem.h>
#include <baresip.h>

#include "aaudio.h"


struct auplay_st {
	mtx_t *mtx;
	AAudioStream *playerStream;
	auplay_write_h *wh;
	void *arg;
	struct auplay_prm play_prm;
	size_t sampsz;
	int32_t device_id;
	bool shutdown;
	bool restarting;
};


static int open_player_stream(struct auplay_st *st);


static void auplay_destructor(void *arg)
{
	struct auplay_st *st = arg;

	info("aaudio: player: closing stream\n");

	if (st->mtx) {
		mtx_lock(st->mtx);
		st->shutdown = true;
		mtx_unlock(st->mtx);
	}

	re_thread_async_cancel((intptr_t)st);

	if (st->mtx)
		mtx_lock(st->mtx);

	if (st->playerStream) {
		AAudioStream_close(st->playerStream);
		st->playerStream = NULL;
	}

	st->wh = NULL;

	if (st->mtx)
		mtx_unlock(st->mtx);

	mem_deref(st->mtx);
}


/**
 * For an output stream, this function should render and write from
 * userData numFrames of data in the streams current data format to
 * the audioData buffer.
 */
 static int dataCallback(AAudioStream *stream, void *userData,
			 void *audioData, int32_t numFrames) {
	(void)stream;
	struct auplay_st *st = userData;
	struct auframe af;

	auframe_init(&af, st->play_prm.fmt, audioData, numFrames,
		     st->play_prm.srate, st->play_prm.ch);

	st->wh(&af, st->arg);

	return 0;
}


static int restart_player_work(void *arg) {
	struct auplay_st *st = arg;
	aaudio_result_t result;

	mtx_lock(st->mtx);

	if (st->shutdown) {
		st->restarting = false;
		mtx_unlock(st->mtx);
		return ECANCELED;
	}

	if (st->playerStream) {
		AAudioStream_close(st->playerStream);
		st->playerStream = NULL;
	}

	result = open_player_stream(st);
	if (result != AAUDIO_OK) {
		st->restarting = false;
		warning("aaudio: player: failed to open stream\n");
		mtx_unlock(st->mtx);
		return ENODEV;
	}

	mtx_unlock(st->mtx);
	return 0;
}


static void restart_player_done(int err, void *arg) {
	struct auplay_st *st = arg;
	aaudio_result_t result;

	mtx_lock(st->mtx);

	if (st->shutdown) {
		st->restarting = false;
		mtx_unlock(st->mtx);
		return;
	}

	if (err) {
		st->restarting = false;
		warning("aaudio: player: open failed: %m\n", err);
		mtx_unlock(st->mtx);
		return;
	}

	result = AAudioStream_requestStart(st->playerStream);
	if (result != AAUDIO_OK)
		warning("aaudio: player: failed to start stream\n");
	else
		info("aaudio: player: stream started\n");

	st->restarting = false;
	mtx_unlock(st->mtx);
}


static void errorCallback(AAudioStream *stream, void *userData,
			  aaudio_result_t error) {
	struct auplay_st *st = userData;
	(void)error;
	int res;

	aaudio_stream_state_t streamState = AAudioStream_getState(stream);
	if (streamState == AAUDIO_STREAM_STATE_DISCONNECTED) {
		info("aaudio: player: stream disconnected\n");

		if (!st->mtx)
			return;

		mtx_lock(st->mtx);
		if (st->shutdown || st->restarting) {
			mtx_unlock(st->mtx);
			return;
		}
		st->restarting = true;
		mtx_unlock(st->mtx);

		res = re_thread_async_id((intptr_t)st, restart_player_work,
					 restart_player_done, st);
		if (res) {
			mtx_lock(st->mtx);
			st->restarting = false;
			mtx_unlock(st->mtx);
			warning("aaudio: player: error queueing task: %m\n",
				res);
		}
	}
}


static int open_player_stream(struct auplay_st *st) {

	AAudioStreamBuilder *builder;
	aaudio_result_t result;
	int32_t device_id = st->device_id;
	bool fallback = false;

 retry:
	result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK) {
		warning("aaudio: player: failed to create stream builder: "
			"error %s\n", AAudio_convertResultToText(result));
		return result;
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStreamBuilder_setSharingMode(builder,
		AAUDIO_SHARING_MODE_SHARED);
	AAudioStreamBuilder_setSampleRate(builder, st->play_prm.srate);
	AAudioStreamBuilder_setChannelCount(builder, 1);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);
	AAudioStreamBuilder_setUsage(builder,
		AAUDIO_USAGE_VOICE_COMMUNICATION);
	AAudioStreamBuilder_setPerformanceMode(builder,
		AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	if (device_id >= 0)
		AAudioStreamBuilder_setDeviceId(builder, device_id);
	AAudioStreamBuilder_setDataCallback(builder, &dataCallback, st);
	AAudioStreamBuilder_setErrorCallback(builder, &errorCallback, st);

	result = AAudioStreamBuilder_openStream(builder, &st->playerStream);
	if (result != AAUDIO_OK) {
		AAudioStreamBuilder_delete(builder);

		if (device_id >= 0 && !fallback) {
			warning("aaudio: player: device %d unavailable, "
				"falling back to default\n", device_id);
			device_id = -1;
			fallback = true;
			goto retry;
		}

		warning("aaudio: player: failed to open stream: error %s\n",
			AAudio_convertResultToText(result));
		return result;
	}

	if (fallback)
		info("aaudio: player: fell back to default device\n");

	info("aaudio: player: opened stream with direction %d, "
	     "sharing mode %d, sample rate %d, format %d, sessionId %d, "
	     "usage %d, performance mode %d\n",
	     AAudioStream_getDirection(st->playerStream),
	     AAudioStream_getSharingMode(st->playerStream),
	     AAudioStream_getSampleRate(st->playerStream),
	     AAudioStream_getFormat(st->playerStream),
	     AAudioStream_getSessionId(st->playerStream),
	     AAudioStream_getUsage(st->playerStream),
	     AAudioStream_getPerformanceMode(st->playerStream));

	AAudioStreamBuilder_delete(builder);

	AAudioStream_setBufferSizeInFrames(st->playerStream,
		AAudioStream_getFramesPerBurst(st->playerStream) * 2);

	return AAUDIO_OK;
}


int aaudio_player_alloc(struct auplay_st **stp, const struct auplay *ap,
	struct auplay_prm *prm, const char *dev, auplay_write_h *wh, void *arg)
{
	struct auplay_st *st;
	aaudio_result_t result;
	int err;

	if (!stp || !ap || !prm || !wh)
		return EINVAL;

	info ("aaudio: opening player (%u Hz, %d channels, device %s, "
		"ptime %u)\n", prm->srate, prm->ch, dev, prm->ptime);

	if (prm->fmt != AUFMT_S16LE) {
		warning("aaudio: player: unsupported sample format (%s)\n",
			aufmt_name((enum aufmt)prm->fmt));
		return ENOTSUP;
	}

	if (prm->ch != 1) {
		warning("aaudio: player: unsupported channel count (%u)\n",
			prm->ch);
		return ENOTSUP;
	}

	st = mem_zalloc(sizeof(*st), auplay_destructor);
	if (!st)
		return ENOMEM;

	err = mutex_alloc(&st->mtx);
	if (err) {
		mem_deref(st);
		return err;
	}

	st->play_prm = *prm;

	/*
	 * Parse device id from the <device> field of audio_player config
	 * (e.g. "audio_player aaudio,5").  "default" or empty means -1
	 * (let AAudio pick the default device).
	 */
	if (str_isset(dev) && str_casecmp(dev, "default") != 0) {
		char *endptr;
		long val = strtol(dev, &endptr, 10);
		if (endptr == dev || *endptr != '\0' || val < 0 ||
		    val > INT32_MAX) {
			warning("aaudio: player: invalid device id "
				"'%s', using default\n", dev);
			st->device_id = -1;
		}
		else {
			st->device_id = (int32_t)val;
		}
	}
	else {
		st->device_id = -1;
	}

	info("aaudio: player: using device id %d\n", st->device_id);

	st->wh  = wh;
	st->arg = arg;

	result = open_player_stream(st);
	if (result != AAUDIO_OK)
		goto out;

	result = AAudioStream_requestStart(st->playerStream);
	if (result != AAUDIO_OK) {
		warning("aaudio: player: failed to start stream\n");
		goto out;
	}

	module_event("aaudio", "player sessionid", NULL, NULL, "%d",
		     AAudioStream_getSessionId(st->playerStream));

	info ("aaudio: player: stream started\n");

  out:
	if (result != AAUDIO_OK) {
		mem_deref(st);
	}
	else
		*stp = st;

	return result;
}
