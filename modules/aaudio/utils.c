/**
 * @file aaudio/utils.c  AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <re.h>
#include <baresip.h>
#include "aaudio.h"


static once_flag aaudio_lock_once = ONCE_FLAG_INIT;
static mtx_t aaudio_global_lock;

static void aaudio_lock_init(void)
{
	(void)mtx_init(&aaudio_global_lock, mtx_recursive);
}

void aaudio_lock(void)
{
	call_once(&aaudio_lock_once, aaudio_lock_init);
	mtx_lock(&aaudio_global_lock);
}

void aaudio_unlock(void)
{
	mtx_unlock(&aaudio_global_lock);
}

void aaudio_close_stream(AAudioStream *stream)
{
	aaudio_stream_state_t state;

	if (!stream)
		return;

	aaudio_lock();
	state = AAudioStream_getState(stream);
	if (state != AAUDIO_STREAM_STATE_CLOSED &&
			state != AAUDIO_STREAM_STATE_CLOSING) {
		(void)AAudioStream_requestStop(stream);
		(void)AAudioStream_close(stream);
	}
	aaudio_unlock();
}
