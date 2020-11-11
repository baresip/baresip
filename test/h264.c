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
	struct h264_nal_header hdr, hdr2;
	struct mbuf *mb;
	static const uint8_t nal = 0x25;
	int err = 0;

	mb = mbuf_alloc(1);
	if (!mb)
		return ENOMEM;

	hdr.f = 0;
	hdr.nri = 1;
	hdr.type = H264_NALU_IDR_SLICE;

	err = h264_nal_header_encode(mb, &hdr);
	if (err)
		goto out;

	ASSERT_EQ(1, mb->pos);
	ASSERT_EQ(1, mb->end);
	ASSERT_EQ(nal, mb->buf[0]);

	mb->pos = 0;

	err = h264_nal_header_decode(&hdr2, mb);
	if (err)
		goto out;

	ASSERT_EQ(1, mb->pos);
	ASSERT_EQ(1, mb->end);

	ASSERT_EQ(0, hdr2.f);
	ASSERT_EQ(1, hdr2.nri);
	ASSERT_EQ(5, hdr2.type);

	ASSERT_TRUE( h264_is_keyframe(H264_NALU_IDR_SLICE));
	ASSERT_TRUE(!h264_is_keyframe(H264_NALU_SLICE));

 out:
	mem_deref(mb);
	return err;
}
