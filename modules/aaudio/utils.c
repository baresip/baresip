/**
 * @file aaudio/utils.c  AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <re.h>
#include <baresip.h>
#include "aaudio.h"


void aaudio_close_stream(AAudioStream *stream)
{
	aaudio_stream_state_t state;

	if (!stream)
		return;

	state = AAudioStream_getState(stream);
	if (state == AAUDIO_STREAM_STATE_CLOSED ||
			state == AAUDIO_STREAM_STATE_CLOSING)
		return;

	(void)AAudioStream_requestStop(stream);
	(void)AAudioStream_close(stream);
}