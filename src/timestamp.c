/**
 * @file timestamp.c  Timestamp helpers
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


/**
 * Check if a 32-bit timestamp wraps around
 *
 * @param ts_new  Timestamp of the current packet
 * @param ts_old  Timestamp of the previous packet
 *
 * @return Integer describing the wrap-around

 * @retval -1  backwards wrap-around
 * @retval  0  no wrap-around
 * @retval  1  forward wrap-around
 */
int timestamp_wrap(uint32_t ts_new, uint32_t ts_old)
{
	int32_t delta;

	if (ts_new < ts_old) {

		delta = (int32_t)ts_new - (int32_t)ts_old;

		if (delta > 0)
			return 1;
	}
	else if ((int32_t)(ts_old - ts_new) > 0) {

		return -1;
	}

	return 0;
}


void timestamp_set(struct timestamp_recv *ts, uint32_t rtp_ts)
{
	if (!ts)
		return;

	ts->first = rtp_ts;
	ts->last  = rtp_ts;

	ts->is_set = true;
}


/**
 * Calculate the total timestamp duration, in timestamp units.
 * The duration is calculated as the delta between the
 * last extended timestamp and the first extended timestamp.
 *
 * @param ts  Receiver timestamp struct
 *
 * @return Timestamp duration
 */
uint64_t timestamp_duration(const struct timestamp_recv *ts)
{
	uint64_t last_ext;

	if (!ts || !ts->is_set)
		return 0;

	last_ext = timestamp_calc_extended(ts->num_wraps, ts->last);

	return last_ext - ts->first;
}


uint64_t timestamp_calc_extended(uint32_t num_wraps, uint32_t ts)
{
	uint64_t ext_ts;

	ext_ts  = (uint64_t)num_wraps * 0x100000000ULL;
	ext_ts += (uint64_t)ts;

	return ext_ts;
}
