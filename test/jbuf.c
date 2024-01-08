/**
 * @file test/jbuf.c Jitterbuffer Testcode
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"

enum { JBUF_SRATE = 8000 };

static uint64_t next_play_value = 0;

struct jbtest {
	uint16_t idx;
	uint16_t seq;
	uint32_t ts;
	uint64_t ts_arrive;
	uint64_t playout;
};

static const struct jbtest testv_20ms[] = {
	/* idx, seq, ts, ts_arrive, playout (ts + (arrival - ts)) */
	{0, 1, 0, 20 * JBUF_SRATE / 1000, 160},
	{1, 2, 160, 40 * JBUF_SRATE / 1000, 320},
	{2, 3, 320, 60 * JBUF_SRATE / 1000, 480},
	{3, 4, 480, 80 * JBUF_SRATE / 1000, 640},
};

static const struct jbtest testv_20ms_reorder[] = {
	/* idx, seq, ts, ts_arrive, playout (ts + (arrival - ts)) */
	{0, 1, 0, 20 * JBUF_SRATE / 1000, 160},
	{2, 3, 320, 60 * JBUF_SRATE / 1000, 480},
	{1, 2, 160, 60 * JBUF_SRATE / 1000, 480},
	{3, 4, 480, 80 * JBUF_SRATE / 1000, 640},
};


static uint64_t next_play(const struct jbuf *jb)
{
	(void)jb;

	return next_play_value;
}


int test_jbuf(void)
{
	struct jbuf *jb;
	struct rtp_header hdr = {0};
	char *frv[4];
	void *mem = NULL;
	int err;

	err = jbuf_alloc(&jb, 0, 10);
	if (err)
		return err;

	jbuf_set_srate(jb, JBUF_SRATE);
	jbuf_set_next_play_fn(jb, next_play);

	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++) {
		frv[i] = mem_alloc(32, NULL);
		if (!frv[i]) {
			err = ENOMEM;
			goto out;
		}
	}

	/* Test empty list */
	ASSERT_EQ(-1, jbuf_next_play(jb));

	for (size_t i = 0; i < RE_ARRAY_SIZE(testv_20ms); i++) {
		struct rtp_header hdr_in = {0}, hdr_out = {0};

		/* Empty list */
		err = jbuf_get(jb, &hdr_out, &mem);
		ASSERT_EQ(ENOENT, err);

		hdr_in.seq	 = testv_20ms[i].seq;
		hdr_in.ts	 = testv_20ms[i].ts;
		hdr_in.ts_arrive = testv_20ms[i].ts_arrive;

		err = jbuf_put(jb, &hdr_in, frv[i]);
		TEST_ERR(err);

		next_play_value = testv_20ms[i].playout;
		ASSERT_EQ(0, jbuf_next_play(jb)); /* already late  test */

		err = jbuf_get(jb, &hdr_out, &mem);
		TEST_ERR(err);
		ASSERT_EQ(hdr_in.seq, hdr_out.seq);
		ASSERT_EQ(mem, frv[i]);
		mem = mem_deref(mem);
	}

	for (size_t i = 0; i < RE_ARRAY_SIZE(testv_20ms_reorder); i++) {
		struct rtp_header hdr_in = {0};

		hdr_in.seq	 = testv_20ms_reorder[i].seq;
		hdr_in.ts	 = testv_20ms_reorder[i].ts;
		hdr_in.ts_arrive = testv_20ms_reorder[i].ts_arrive;

		err = jbuf_put(jb, &hdr_in, frv[i]);
		TEST_ERR(err);
	}

	for (size_t i = 0; i < RE_ARRAY_SIZE(testv_20ms_reorder); i++) {
		struct rtp_header hdr_out = {0};
		next_play_value = testv_20ms_reorder[i].playout;

		err = jbuf_get(jb, &hdr_out, &mem);
		ASSERT_TRUE(err == 0 || err == EAGAIN);
		ASSERT_EQ(i + 1, hdr_out.seq);
		ASSERT_EQ(mem, frv[testv_20ms_reorder[i].idx]);
		mem = mem_deref(mem);
	}

	ASSERT_EQ(ENOENT, jbuf_get(jb, &hdr, &mem));

	/* Test jbuf_next_play */
	{
		struct rtp_header hdr_in = {0}, hdr_out = {0};

		jbuf_flush(jb);

		hdr_in.seq = 1;
		hdr_in.ts = 160;
		hdr_in.ts_arrive = 0;
		err = jbuf_put(jb, &hdr_in, frv[0]);
		TEST_ERR(err);
	
		hdr_in.seq = 2;
		hdr_in.ts = 320;
		hdr_in.ts_arrive = 160;
		err = jbuf_put(jb, &hdr_in, frv[1]);
		TEST_ERR(err);

		next_play_value = 0;

		err = jbuf_get(jb, &hdr_out, &mem);
		TEST_ERR(err);
		mem = mem_deref(mem);

		/* Wait 20ms for next packet */
		ASSERT_EQ(20, jbuf_next_play(jb));

		next_play_value = 160;

		err = jbuf_get(jb, &hdr_out, &mem);
		TEST_ERR(err);
		mem = mem_deref(mem);
	}

 out:
	mem_deref(jb);
	mem_deref(mem);
	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++)
		mem_deref(frv[i]);

	return err;
}


int test_jbuf_adaptive(void)
{
	struct rtp_header hdr = {0};
	struct jbuf *jb = NULL;
	char *frv[4];
	uint32_t latency = 100; /* [ms] */
	void *mem = NULL;
	int err;

	hdr.ssrc = 1;

	err = jbuf_alloc(&jb, latency, 10);
	TEST_ERR(err);
	err = jbuf_set_type(jb, JBUF_ADAPTIVE);
	TEST_ERR(err);

	jbuf_set_srate(jb, JBUF_SRATE);
	jbuf_set_next_play_fn(jb, next_play);

	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++) {
		frv[i] = mem_zalloc(32, NULL);
		if (frv[i] == NULL) {
			err = ENOMEM;
			goto out;
		}
	}

	for (size_t i = 0; i < RE_ARRAY_SIZE(testv_20ms); i++) {
		struct rtp_header hdr_in = {0}, hdr_out = {0};

		/* Empty list */
		err = jbuf_get(jb, &hdr_out, &mem);
		ASSERT_EQ(ENOENT, err);

		hdr_in.seq = testv_20ms[i].seq;
		hdr_in.ts = testv_20ms[i].ts;
		hdr_in.ts_arrive = testv_20ms[i].ts_arrive;

		err = jbuf_put(jb, &hdr_in, frv[i]);
		TEST_ERR(err);

		next_play_value = testv_20ms[i].playout +
				  (latency * JBUF_SRATE / 1000);
		err = jbuf_get(jb, &hdr_out, &mem);
		TEST_ERR(err);
		ASSERT_EQ(hdr_in.seq, hdr_out.seq);
		ASSERT_EQ(mem, frv[i]);
		mem = mem_deref(mem);
	}

 out:
	mem_deref(jb);
	mem_deref(mem);
	for (size_t i = 0; i < RE_ARRAY_SIZE(frv); i++)
		mem_deref(frv[i]);

	return err;
}
