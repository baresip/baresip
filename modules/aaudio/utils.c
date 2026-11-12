/**
 * @file aaudio/utils.c  AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <re.h>
#include <baresip.h>
#include "aaudio.h"


void aaudio_close_stream(mtx_t *mtx, AAudioStream **streamp) {
	AAudioStream *stream = NULL;

	if (mtx)
		mtx_lock(mtx);
	if (streamp && *streamp) {
		stream = *streamp;
		*streamp = NULL;
	}
	if (mtx)
		mtx_unlock(mtx);

	if (stream)
		AAudioStream_close(stream);
}
