/**
 * @file aaudio/utils.c  AAudio audio driver for Android
 *
 * Copyright (C) 2024 Juha Heinanen
 */

#include <re.h>
#include <baresip.h>
#include "aaudio.h"


void aaudio_close_stream(AAudioStream *stream) {
	if (stream)
		AAudioStream_close(stream);
}
