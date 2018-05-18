/**
 * @file vidutil.c  Video utility functions
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "core.h"


/**
 * Calculate the RTP timestamp from Presentation Time Stamp (PTS)
 * or Decoding Time Stamp (DTS) and framerate.
 *
 * @note The calculated RTP Timestamp may NOT wrap around.
 *
 * @param pts Presentation Time Stamp (PTS)
 * @param fps Framerate in [frames per second]
 *
 * @return Extended RTP Timestamp
 */
uint64_t video_calc_rtp_timestamp(int64_t pts, double fps)
{
	uint64_t rtp_ts;

	if (!fps)
		return 0;

	rtp_ts = ((uint64_t)VIDEO_SRATE * pts) / fps;

	return rtp_ts;
}


/**
 * Calculate the timestamp in seconds from the RTP timestamp.
 *
 * @param rtp_ts RTP Timestamp
 *
 * @return Timestamp in seconds
 */
double video_calc_seconds(uint64_t rtp_ts)
{
	double timestamp;

	/* convert from RTP clockrate to seconds */
	timestamp = (double)rtp_ts / (double)VIDEO_SRATE;

	return timestamp;
}


/**
 * Convert a video timestamp to seconds
 *
 * @param timestamp Timestamp in VIDEO_TIMEBASE units
 *
 * @return Timestamp in seconds
 */
double video_timestamp_to_seconds(uint64_t timestamp)
{
	return (double)timestamp / (double)VIDEO_TIMEBASE;
}


/**
 * Calculate the RTP timestamp from a timestamp in VIDEO_TIMEBASE units
 *
 * @param timestamp Timestamp in VIDEO_TIMEBASE units
 *
 * @return Extended RTP Timestamp
 */
uint64_t video_calc_rtp_timestamp_fix(uint64_t timestamp)
{
	uint64_t rtp_ts;

	rtp_ts = timestamp * VIDEO_SRATE / VIDEO_TIMEBASE;

	return rtp_ts;
}
