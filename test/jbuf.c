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
	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	/* One frame */
	memset(&hdr, 0, sizeof(hdr));
	hdr.seq = 160;
	hdr.ts = 1;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	err = jbuf_put(jb, &hdr, frv[0]);
	ASSERT_EQ(EALREADY, err);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(160, hdr2.seq);
	ASSERT_EQ(frv[0], mem);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	/* Two frames */
	hdr.seq = 161;
	hdr.ts = 1;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	hdr.seq = 162;
	hdr.ts = 2;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(161, hdr2.seq);
	ASSERT_EQ(frv[0], mem);
	mem = mem_deref(mem);

	hdr.seq = 163;
	hdr.ts = 3;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(162, hdr2.seq);
	ASSERT_EQ(frv[1], mem);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	/* Three frames */
	hdr.seq = 165;
	hdr.ts = 5;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	hdr.seq = 164;
	hdr.ts = 4;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	hdr.seq = 166;
	hdr.ts = 6;
	err = jbuf_put(jb, &hdr, frv[2]);
	TEST_ERR(err);

	/* nfa <= jb->nf*JBUF_EMA_FAC */
	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(163, hdr2.seq);
	ASSERT_EQ(frv[0], mem);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(164, hdr2.seq);
	ASSERT_EQ(frv[0], mem);
	mem = mem_deref(mem);

	hdr.seq = 167;
	hdr.ts = 7;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(165, hdr2.seq);
	ASSERT_EQ(frv[1], mem);
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


int test_jbuf_frames(void)
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

	for (i=0; i<RE_ARRAY_SIZE(frv); i++) {
		frv[i] = mem_zalloc(32, NULL);
		if (frv[i] == NULL) {
			err = ENOMEM;
			goto out;
		}
	}

	/* Empty list */
	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	/* Two frames */
	hdr.seq = 160;
	hdr.ts = 160;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);
	err = jbuf_put(jb, &hdr, frv[0]);
	ASSERT_EQ(EALREADY, err);

	/* not able to decide that frame is complete */
	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	hdr.seq = 161;
	hdr.ts = 161;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	hdr.seq = 162;
	hdr.ts = 162;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	/* detected complete frame */
	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err);
	ASSERT_EQ(160, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(ENOENT, err);

	/* Four  frames */
	jbuf_flush(jb);
	hdr.seq = hdr.ts = 1;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

	hdr.seq = hdr.ts = 2;
	err = jbuf_put(jb, &hdr, frv[1]);
	TEST_ERR(err);

	hdr.seq = hdr.ts = 3;
	err = jbuf_put(jb, &hdr, frv[2]);
	TEST_ERR(err);

	hdr.seq = hdr.ts = 4;
	err = jbuf_put(jb, &hdr, frv[3]);
	TEST_ERR(err);

	err = jbuf_get(jb, &hdr2, &mem);
	ASSERT_EQ(EAGAIN, err);
	ASSERT_EQ(1, hdr2.seq);
	ASSERT_EQ(mem, frv[0]);
	mem = mem_deref(mem);

	err = jbuf_get(jb, &hdr2, &mem);
	TEST_ERR(err)
	ASSERT_EQ(2, hdr2.seq);
	ASSERT_EQ(mem, frv[1]);
	mem = mem_deref(mem);

	hdr.seq = hdr.ts = 5;
	err = jbuf_put(jb, &hdr, frv[0]);
	TEST_ERR(err);

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


int test_jbuf_video_frames(void)
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

	err = jbuf_alloc(&jb, 1, 2);
	TEST_ERR(err);

	err = jbuf_resize(jb, 10);
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
	TEST_ERR(err);
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
