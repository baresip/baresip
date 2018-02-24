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

	/* test with framerate of zero */
	ASSERT_EQ(0, video_calc_rtp_timestamp(1, 0));

	ASSERT_EQ(         0, video_calc_rtp_timestamp(      0, 30));
	ASSERT_EQ(      3000, video_calc_rtp_timestamp(      1, 30));
	ASSERT_EQ(     30000, video_calc_rtp_timestamp(     10, 30));
	ASSERT_EQ(    300000, video_calc_rtp_timestamp(    100, 30));
	ASSERT_EQ(   3000000, video_calc_rtp_timestamp(   1000, 30));
	ASSERT_EQ(  30000000, video_calc_rtp_timestamp(  10000, 30));
	ASSERT_EQ( 300000000, video_calc_rtp_timestamp( 100000, 30));
	ASSERT_EQ(3000000000, video_calc_rtp_timestamp(1000000, 30));

	ASSERT_EQ(4294965000ULL, video_calc_rtp_timestamp(1431655, 30));
	ASSERT_EQ(4294968000ULL, video_calc_rtp_timestamp(1431656, 30));

 out:
	return err;
}
