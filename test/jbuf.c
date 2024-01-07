/**
 * @file test/jbuf.c Jitterbuffer Testcode
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"

enum { JITTER_SRATE = 8000 };


int test_jbuf(void)
{
	struct rtp_header hdr = {0}, hdr2 = {0};
	struct jbuf *jb;
	char *frv[3];
	void *mem = NULL;
	int err;

	err = jbuf_alloc(&jb, 0, 10);
	if (err)
		return err;

	jbuf_set_srate(jb, JITTER_SRATE);

	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++) {
		frv[i] = mem_alloc(32, NULL);
		if (!frv[i]) {
			err = ENOMEM;
			goto out;
		}
	}

	/* Empty list */
	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	/* One frame */
	hdr.seq = 160;
	hdr.ts = 1;
	hdr.ts_arrive = tmr_jiffies() * JITTER_SRATE / 1000;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	err = jbuf_put(jb, &hdr, frv[0]);
	ASSERT_EQ(EALREADY, err);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);

	ASSERT_EQ(160, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr2, &mem));

	/* Two frames */
	hdr.seq = 320;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	hdr.seq = 480;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(320, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(480, hdr2.seq);
	ASSERT_EQ(mem, frv[1]);
	mem = mem_deref(mem);

	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr2, &mem));

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
	ASSERT_EQ(640, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(800, hdr2.seq);
	ASSERT_EQ(mem, frv[1]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(960, hdr2.seq);
	ASSERT_EQ(mem, frv[2]);
	mem = mem_deref(mem);

	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr2, &mem));


 out:
	mem_deref(jb);
	mem_deref(mem);
	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++)
		mem_deref(frv[i]);

	return err;
}


int test_jbuf_adaptive(void)
{
	struct rtp_header hdr = {0}, hdr2 = {0};
	struct jbuf *jb = NULL;
	char *frv[4];
	void *mem = NULL;
	int32_t next_play;
	int err;

	hdr.ssrc = 1;

	err = jbuf_alloc(&jb, 1, 10);
	TEST_ERR(err);
	err = jbuf_set_type(jb, JBUF_ADAPTIVE);
	TEST_ERR(err);

	jbuf_set_srate(jb, JITTER_SRATE);

	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++) {
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
	hdr.ts_arrive = tmr_jiffies() * JITTER_SRATE / 1000;

	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	ASSERT_EQ(EALREADY, jbuf_put(jb, &hdr, frv[0]));

	/* min latency is not reached yet */
	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr2, &mem));

	hdr.seq = 161;
	hdr.ts = 161;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	next_play = jbuf_next_play(jb);
	ASSERT_EQ(1, next_play);
	sys_msleep(next_play);

	/* min latency reached */
	ASSERT_EQ(EAGAIN, jbuf_get(jb, &hdr2, &mem));
	ASSERT_EQ(160, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(161, hdr2.seq);
	ASSERT_EQ(mem, frv[1]);
	mem = mem_deref(mem);

	/* Four  frames */
	jbuf_flush(jb);

	/* Test empty packetl */
	next_play = jbuf_next_play(jb);
	ASSERT_EQ(-1, next_play);

	hdr.seq = 1;
	hdr.ts = 100;
	hdr.ts_arrive = tmr_jiffies() * JITTER_SRATE / 1000;

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

	next_play = jbuf_next_play(jb);
	ASSERT_EQ(0, next_play);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(0, err);
	ASSERT_EQ(1, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	next_play = jbuf_next_play(jb);
	ASSERT_EQ(1, next_play);
	sys_msleep(next_play);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(2, hdr2.seq);
	ASSERT_EQ(mem, frv[1]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(3, hdr2.seq);
	ASSERT_EQ(mem, frv[2]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(0, err);
	ASSERT_EQ(4, hdr2.seq);
	ASSERT_EQ(mem, frv[3]);
	mem = mem_deref(mem);

	err = 0;

 out:
	mem_deref(jb);
	mem_deref(mem);
	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++)
		mem_deref(frv[i]);

	return err;
}
