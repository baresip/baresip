/**
 * @file timer.c  Timer functions
 *
 * Copyright (C) 2010 Creytiv.com
 */

#define _BSD_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <time.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Get the timer jiffies in microseconds [us]
 *
 * @return Jiffies in [us]
 */
uint64_t tmr_jiffies_usec(void)
{
	uint64_t jfs;

#if defined(WIN32)
	FILETIME ft;
	ULARGE_INTEGER li;
	GetSystemTimeAsFileTime(&ft);
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	jfs = li.QuadPart/10;
#else
	struct timespec now;
	clockid_t clock_id;

#if defined (CLOCK_BOOTTIME)
	clock_id = CLOCK_BOOTTIME;
#else
	clock_id = CLOCK_MONOTONIC;
#endif

	if (0 != clock_gettime(clock_id, &now)) {
		warning("timer: clock_gettime() failed (%m)\n", errno);
		return 0;
	}

	jfs  = (long)now.tv_sec * (uint64_t)1000000;
	jfs += now.tv_nsec / (uint64_t)1000;

#endif

	return jfs;
}
