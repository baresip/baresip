/**
 * @file test/jbuf.c Jitterbuffer Testcode
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


int test_jbuf(void)
{
	struct rtp_header hdr, hdr2;
	struct jbuf *jb;
	char *frv[3];
	uint32_t i;
	void *mem = NULL;
	int err;

	memset(frv, 0, sizeof(frv));

	err = jbuf_alloc(&jb, 0, 10);
	if (err)
		return err;

	for (i=0; i<RE_ARRAY_SIZE(frv); i++) {
		frv[i] = mem_alloc(32, NULL);
		if (!frv[i]) {
			err = ENOMEM;
			goto out;
		}
	}

	/* Empty list */
	if (ENOENT != jbuf_get(jb, &hdr2, &mem)) {
		err = EINVAL;
		goto out;
	}


	/* One frame */
	memset(&hdr, 0, sizeof(hdr));
	hdr.seq = 160;
	hdr.ts = 1;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	if ((EALREADY != jbuf_put(jb, &hdr, frv[0]))) {err = EINVAL; goto out;}

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	if (160 != hdr2.seq) {err = EINVAL; goto out;}
	if (mem != frv[0]) {err = EINVAL; goto out;}
	mem = mem_deref(mem);

	if (ENOENT != jbuf_get(jb, &hdr2, &mem)) {err = EINVAL; goto out;}


	/* Two frames */
	hdr.seq = 320;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	hdr.seq = 480;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	if (320 != hdr2.seq) {err = EINVAL; goto out;}
	if (mem != frv[0]) {err = EINVAL; goto out;}
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	if (480 != hdr2.seq) {err = EINVAL; goto out;}
	if (mem != frv[1]) {err = EINVAL; goto out;}
	mem = mem_deref(mem);

	if (ENOENT != jbuf_get(jb, &hdr2, &mem)) {err = EINVAL; goto out;}


	/* Three frames */
	hdr.seq = 800;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	hdr.seq = 640;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	hdr.seq = 960;
	err = jbuf_put(jb, &hdr, frv[2]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	if (640 != hdr2.seq) {err = EINVAL; goto out;}
	if (mem != frv[0]) {err = EINVAL; goto out;}
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	if (800 != hdr2.seq) {err = EINVAL; goto out;}
	if (mem != frv[1]) {err = EINVAL; goto out;}
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	if (960 != hdr2.seq) {err = EINVAL; goto out;}
	if (mem != frv[2]) {err = EINVAL; goto out;}
	mem = mem_deref(mem);

	if (ENOENT != jbuf_get(jb, &hdr2, &mem)) {err = EINVAL; goto out;}


 out:
	mem_deref(jb);
	mem_deref(mem);
	for (i=0; i<RE_ARRAY_SIZE(frv); i++)
		mem_deref(frv[i]);

	return err;
}


int test_jbuf_adaptive(void)
{
	struct rtp_header hdr, hdr2;
	struct jbuf *jb = NULL;
	char *frv[4];
	uint32_t i;
	void *mem = NULL;
	int err;

	memset(frv, 0, sizeof(frv));
	memset(&hdr, 0, sizeof(hdr));
	memset(&hdr2, 0, sizeof(hdr2));
	hdr.ssrc = 1;

	err = jbuf_alloc(&jb, 1, 10);
	TEST_ERR(err);
	err = jbuf_set_type(jb, JBUF_ADAPTIVE);
	TEST_ERR(err);

	for (i=0; i<RE_ARRAY_SIZE(frv); i++) {
		frv[i] = mem_zalloc(32, NULL);
		if (frv[i] == NULL) {
			err = ENOMEM;
			goto out;
		}
	}

	/* Empty list */
	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr2, &mem));

	/* Two frames */
	hdr.seq = 160;
	hdr.ts = 160;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	ASSERT_EQ(EALREADY, jbuf_put(jb, &hdr, frv[0]));

	/* wish size is not reached yet */
	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr2, &mem));

	hdr.seq = 161;
	hdr.ts = 161;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	/* wish size reached */
	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(160, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr2, &mem));

	/* Four  frames */
	jbuf_flush(jb);
	hdr.seq = 1;
	hdr.ts = 100;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	hdr.seq = 2;
	hdr.ts = 200;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	hdr.seq = 3;
	hdr.ts = 300;
	err = jbuf_put(jb, &hdr, frv[2]);
	TEST_ERR(err);

	hdr.seq = 4;
	hdr.ts = 400;
	err = jbuf_put(jb, &hdr, frv[3]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(1, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(2, hdr2.seq);
	ASSERT_EQ(mem, frv[1]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(0, err);
	ASSERT_EQ(3, hdr2.seq);
	ASSERT_EQ(mem, frv[2]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	err = 0;

 out:
	mem_deref(jb);
	mem_deref(mem);
	for (i=0; i<RE_ARRAY_SIZE(frv); i++)
		mem_deref(frv[i]);

	return err;
}


int test_jbuf_adaptive_video(void)
{
	struct rtp_header hdr, hdr2;
	struct jbuf *jb = NULL;
	char *frv[5];
	uint32_t i;
	void *mem = NULL;
	int err;

	memset(frv, 0, sizeof(frv));
	memset(&hdr, 0, sizeof(hdr));
	memset(&hdr2, 0, sizeof(hdr2));
	hdr.ssrc = 1;

	err = jbuf_alloc(&jb, 1, 10);
	TEST_ERR(err);
	err = jbuf_set_type(jb, JBUF_ADAPTIVE);
	TEST_ERR(err);

	for (i=0; i<RE_ARRAY_SIZE(frv); i++) {
		frv[i] = mem_zalloc(32, NULL);
		if (frv[i] == NULL) {
			err = ENOMEM;
			goto out;
		}
	}


	/* --- Test unordered insert --- */
	jbuf_flush(jb);

	hdr.seq = 1;
	hdr.ts = 100;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	ASSERT_EQ(1, jbuf_frames(jb));
	ASSERT_EQ(1, jbuf_packets(jb));

	hdr.seq = 2;
	hdr.ts = 100; /* Same frame */
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);
	ASSERT_EQ(1, jbuf_frames(jb));
	ASSERT_EQ(2, jbuf_packets(jb));

	hdr.seq = 4;
	hdr.ts = 200;
	err = jbuf_put(jb, &hdr, frv[2]);
	TEST_ERR(err);
	ASSERT_EQ(2, jbuf_frames(jb));
	ASSERT_EQ(3, jbuf_packets(jb));

	hdr.seq = 3; /* unordered late packet */
	hdr.ts = 200;
	err = jbuf_put(jb, &hdr, frv[3]);
	TEST_ERR(err);
	ASSERT_EQ(2, jbuf_frames(jb));
	ASSERT_EQ(4, jbuf_packets(jb));

	hdr.seq = 5;
	hdr.ts = 300;
	err = jbuf_put(jb, &hdr, frv[4]);
	TEST_ERR(err);
	ASSERT_EQ(3, jbuf_frames(jb));
	ASSERT_EQ(5, jbuf_packets(jb));

	/* --- Test late packet, unique frame --- */
	jbuf_flush(jb);

	hdr.seq = 1;
	hdr.ts = 100;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	ASSERT_EQ(1, jbuf_frames(jb));
	ASSERT_EQ(1, jbuf_packets(jb));

	hdr.seq = 2;
	hdr.ts = 100; /* Same frame */
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);
	ASSERT_EQ(1, jbuf_frames(jb));
	ASSERT_EQ(2, jbuf_packets(jb));

	hdr.seq = 4;
	hdr.ts = 300;
	err = jbuf_put(jb, &hdr, frv[2]);
	TEST_ERR(err);
	ASSERT_EQ(2, jbuf_frames(jb));
	ASSERT_EQ(3, jbuf_packets(jb));

	hdr.seq = 3; /* unordered late packet */
	hdr.ts = 200;
	err = jbuf_put(jb, &hdr, frv[3]);
	TEST_ERR(err);
	ASSERT_EQ(3, jbuf_frames(jb));
	ASSERT_EQ(4, jbuf_packets(jb));

	/* --- Test lost get --- */
	jbuf_flush(jb);

	hdr.seq = 1;
	hdr.ts = 100;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	ASSERT_EQ(1, jbuf_frames(jb));
	ASSERT_EQ(1, jbuf_packets(jb));

	hdr.seq = 2;
	hdr.ts = 100; /* Same frame */
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);
	ASSERT_EQ(1, jbuf_frames(jb));
	ASSERT_EQ(2, jbuf_packets(jb));

	/* LOST hdr.seq = 3; */

	hdr.seq = 4;
	hdr.ts = 200;
	err = jbuf_put(jb, &hdr, frv[2]);
	TEST_ERR(err);
	ASSERT_EQ(2, jbuf_frames(jb));
	ASSERT_EQ(3, jbuf_packets(jb));

	hdr.seq = 5;
	hdr.ts = 300;
	err = jbuf_put(jb, &hdr, frv[3]);
	TEST_ERR(err);
	ASSERT_EQ(3, jbuf_frames(jb));
	ASSERT_EQ(4, jbuf_packets(jb));

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	mem = mem_deref(mem);
	ASSERT_EQ(3, jbuf_frames(jb));
	ASSERT_EQ(3, jbuf_packets(jb));

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	mem = mem_deref(mem);
	ASSERT_EQ(2, jbuf_frames(jb));
	ASSERT_EQ(2, jbuf_packets(jb));

	err = 0;

 out:
	mem_deref(jb);
	mem_deref(mem);
	for (i=0; i<RE_ARRAY_SIZE(frv); i++)
		mem_deref(frv[i]);

	return err;
}
