/**
 * @file test/video.c  Baresip selftest -- video
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "video"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


int test_video(void)
{
	int err = 0;

	ASSERT_EQ(0,        video_calc_rtp_timestamp_fix(0));
	ASSERT_EQ(90,       video_calc_rtp_timestamp_fix(1000));
	ASSERT_EQ(90000,    video_calc_rtp_timestamp_fix(1000000));
	ASSERT_EQ(90000000, video_calc_rtp_timestamp_fix(1000000000));

	ASSERT_EQ(0,          video_calc_timebase_timestamp(0));
	ASSERT_EQ(1000,       video_calc_timebase_timestamp(90));
	ASSERT_EQ(1000000,    video_calc_timebase_timestamp(90000));
	ASSERT_EQ(1000000000, video_calc_timebase_timestamp(90000000));

 out:
	return err;
}
