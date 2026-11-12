/**
 * @file aaudio/utils.c  AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <pthread.h>
#include <re.h>
#include <baresip.h>
#include "aaudio.h"


void aaudio_close_stream(pthread_mutex_t *lock, AAudioStream **streamp) {
	AAudioStream *stream = NULL;

	if (lock)
		pthread_mutex_lock(lock);
	if (streamp && *streamp) {
		stream = *streamp;
		*streamp = NULL;
	}
	if (lock)
		pthread_mutex_unlock(lock);

	if (stream) {
		AAudioStream_close(stream);
	}
}
