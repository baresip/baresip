/**
 * @file test/h264.c  Baresip selftest -- H.264 code
 *
 * Copyright (C) 2020 Alfred E. Heggestad
 */

#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "test.h"


int test_h264(void)
{
	int err = 0;

	ASSERT_TRUE( h264_is_keyframe(H264_NALU_IDR_SLICE));
	ASSERT_TRUE(!h264_is_keyframe(H264_NALU_SLICE));

 out:
	return err;
}
