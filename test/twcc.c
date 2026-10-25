/**
 * @file test/twcc.c TWCC Testcode
 *
 * Copyright (C) 2025 Sebastian Reimers
 */
#include <re.h>
#include <baresip.h>
#include "test.h"

enum { TS_BASE = 5000000 }; /* realistic arrival time in [ms] */


/* first sequence numbers straddle the uint16_t boundary */
static int test_twcc_seqwrap(void)
{
	struct twcc_status *s = NULL;
	struct twcc *twcc;
	int err;

	err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	twcc_status_append(s, 65534, 100);
	twcc_status_append(s, 65535, 101);
	twcc_status_append(s, 0, 102); /* wrap */
	twcc_status_append(s, 1, 103);

	twcc_status_send_feedback(s);

	twcc = twcc_status_msg(s);
	ASSERT_EQ(65534, twcc->seq);
	ASSERT_EQ(4, twcc->count);
	ASSERT_EQ(1, twcc->reftime);
	ASSERT_EQ(2, (int)mbuf_get_left(twcc->chunks));
	ASSERT_EQ(0x2004, ntohs(mbuf_read_u16(twcc->chunks)));

	ASSERT_EQ(4, (int)mbuf_get_left(twcc->deltas));
	ASSERT_EQ((100 - 64) * 4, mbuf_read_u8(twcc->deltas));
	for (int i = 0; i < 3; i++)
		ASSERT_EQ(4, mbuf_read_u8(twcc->deltas));

out:
	mem_deref(s);
	return err;
}


/* lost packets must not split the feedback or reset the reference time */
static int test_twcc_loss(void)
{
	struct twcc_status *s = NULL;
	struct twcc *twcc;
	int err;

	err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	twcc_status_append(s, 100, TS_BASE);
	twcc_status_append(s, 103, TS_BASE + 20);

	twcc_status_send_feedback(s);

	twcc = twcc_status_msg(s);
	ASSERT_EQ(100, twcc->seq);
	ASSERT_EQ(4, twcc->count);
	ASSERT_EQ(TS_BASE / 64, twcc->reftime);

	ASSERT_EQ(6, (int)mbuf_get_left(twcc->chunks));
	ASSERT_EQ(0x2001, ntohs(mbuf_read_u16(twcc->chunks)));
	ASSERT_EQ(0x0002, ntohs(mbuf_read_u16(twcc->chunks)));
	ASSERT_EQ(0x2001, ntohs(mbuf_read_u16(twcc->chunks)));

	ASSERT_EQ(2, (int)mbuf_get_left(twcc->deltas));
	ASSERT_EQ((TS_BASE % 64) * 4, mbuf_read_u8(twcc->deltas));
	ASSERT_EQ(80, mbuf_read_u8(twcc->deltas));

	/* a feedback starting with lost packets */
	twcc_status_append(s, 106, TS_BASE + 40);

	twcc_status_send_feedback(s);

	ASSERT_EQ(104, twcc->seq);
	ASSERT_EQ(3, twcc->count);
	ASSERT_EQ((TS_BASE + 40) / 64, twcc->reftime);

	ASSERT_EQ(4, (int)mbuf_get_left(twcc->chunks));
	ASSERT_EQ(0x0002, ntohs(mbuf_read_u16(twcc->chunks)));
	ASSERT_EQ(0x2001, ntohs(mbuf_read_u16(twcc->chunks)));

	ASSERT_EQ(1, (int)mbuf_get_left(twcc->deltas));
	ASSERT_EQ(((TS_BASE + 40) % 64) * 4, mbuf_read_u8(twcc->deltas));

out:
	mem_deref(s);
	return err;
}


/* a large delta needs two bit status symbols */
static int test_twcc_large_delta(void)
{
	struct twcc_status *s = NULL;
	struct twcc *twcc;
	uint64_t ts = TS_BASE;
	int err;

	err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	/* Status Vector Chunk - 7 x 2 bits */
	twcc_status_append(s, 0, ts);
	twcc_status_append(s, 2, ts += 100); /* large delta */
	for (uint16_t i = 3; i < 7; i++)
		twcc_status_append(s, i, ++ts);

	/* 8 small deltas followed by a large one */
	twcc_status_append(s, 7, ++ts);
	for (uint16_t i = 9; i < 16; i++)
		twcc_status_append(s, i, ++ts);
	twcc_status_append(s, 16, ts + 100); /* large delta */

	twcc_status_send_feedback(s);

	twcc = twcc_status_msg(s);
	ASSERT_EQ(17, twcc->count);

	ASSERT_EQ(10, (int)mbuf_get_left(twcc->chunks));
	ASSERT_EQ(0xd255, ntohs(mbuf_read_u16(twcc->chunks)));
	ASSERT_EQ(0x2001, ntohs(mbuf_read_u16(twcc->chunks)));
	ASSERT_EQ(0x0001, ntohs(mbuf_read_u16(twcc->chunks)));
	ASSERT_EQ(0x2007, ntohs(mbuf_read_u16(twcc->chunks)));
	ASSERT_EQ(0x4001, ntohs(mbuf_read_u16(twcc->chunks)));

	ASSERT_EQ(1 + 2 + 4 + 8 + 2, (int)mbuf_get_left(twcc->deltas));
	ASSERT_EQ((TS_BASE % 64) * 4, mbuf_read_u8(twcc->deltas));
	ASSERT_EQ(400, ntohs(mbuf_read_u16(twcc->deltas)));
	for (int i = 0; i < 12; i++)
		ASSERT_EQ(4, mbuf_read_u8(twcc->deltas));
	ASSERT_EQ(400, ntohs(mbuf_read_u16(twcc->deltas)));

out:
	mem_deref(s);
	return err;
}


static int test_twcc_duplicate(void)
{
	struct twcc_status *s = NULL;
	struct twcc *twcc;
	int err;

	err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	twcc_status_append(s, 40000, TS_BASE);
	twcc_status_append(s, 40001, TS_BASE + 1);
	twcc_status_append(s, 40001, TS_BASE + 2); /* duplicate */
	twcc_status_append(s, 40002, TS_BASE + 3);

	twcc_status_send_feedback(s);

	twcc = twcc_status_msg(s);
	ASSERT_EQ(40000, twcc->seq);
	ASSERT_EQ(3, twcc->count);
	ASSERT_EQ(2, (int)mbuf_get_left(twcc->chunks));
	ASSERT_EQ(0x2003, ntohs(mbuf_read_u16(twcc->chunks)));

out:
	mem_deref(s);
	return err;
}


/* a skipped gap must start a new feedback message */
static int test_twcc_discontinuity(void)
{
	struct twcc_status *s = NULL;
	struct twcc *twcc;
	int err;

	err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	twcc_status_append(s, 10, TS_BASE);
	twcc_status_append(s, 11, TS_BASE + 1);
	twcc_status_append(s, 20000, TS_BASE + 2); /* gap is not filled */
	twcc_status_append(s, 20001, TS_BASE + 3);

	twcc_status_send_feedback(s);

	twcc = twcc_status_msg(s);
	ASSERT_EQ(10, twcc->seq);
	ASSERT_EQ(2, twcc->count);
	ASSERT_EQ(2, (int)mbuf_get_left(twcc->chunks));
	ASSERT_EQ(0x2002, ntohs(mbuf_read_u16(twcc->chunks)));

	twcc_status_send_feedback(s);

	ASSERT_EQ(20000, twcc->seq);
	ASSERT_EQ(2, twcc->count);
	ASSERT_EQ(2, (int)mbuf_get_left(twcc->chunks));
	ASSERT_EQ(0x2002, ntohs(mbuf_read_u16(twcc->chunks)));

out:
	mem_deref(s);
	return err;
}


int test_twcc(void)
{
	struct twcc_status *s = NULL;
	uint16_t chunk;
	struct twcc *twcc;
	uint32_t i;

	int err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	err = test_twcc_seqwrap();
	TEST_ERR(err);

	err = test_twcc_loss();
	TEST_ERR(err);

	err = test_twcc_large_delta();
	TEST_ERR(err);

	err = test_twcc_duplicate();
	TEST_ERR(err);

	err = test_twcc_discontinuity();
	TEST_ERR(err);

	/* --- Packet 1 --- */

	for (i = 0; i < 4; i++) {
		twcc_status_append(s, i, i + 1);
	}
	twcc_status_append(s, 13, 13);
	twcc_status_append(s, 12, 12); /* reordered, not yet reported */

	twcc_status_send_feedback(s);

	twcc = twcc_status_msg(s);
	ASSERT_EQ(14, twcc->count);
	ASSERT_EQ(2, (int)mbuf_get_left(twcc->chunks));
	chunk  = ntohs(mbuf_read_u16(twcc->chunks));
	ASSERT_EQ(0xbc03, chunk);
	ASSERT_EQ(0, (int)mbuf_get_left(twcc->chunks));

	ASSERT_EQ(6, (int)mbuf_get_left(twcc->deltas));
	uint16_t delta;
	for (i = 0; i < 4; i++) {
		delta = mbuf_read_u8(twcc->deltas);
		ASSERT_EQ(4, delta);
	}
	delta = mbuf_read_u8(twcc->deltas);
	ASSERT_EQ(32, delta);
	delta = mbuf_read_u8(twcc->deltas);
	ASSERT_EQ(4, delta);
	ASSERT_EQ(0, (int)mbuf_get_left(twcc->deltas));

	/* --- Packet 2 --- */

	for (i = 14; i < 40; i++) {
		twcc_status_append(s, i, i);
	}
	twcc_status_append(s, 50, 200);
	twcc_status_append(s, 51, 201);
	twcc_status_send_feedback(s);
	chunk = ntohs(mbuf_read_u16(twcc->chunks));
	ASSERT_EQ(0x201a, chunk);

	chunk = ntohs(mbuf_read_u16(twcc->chunks));
	ASSERT_EQ(0xa, chunk);
	chunk = ntohs(mbuf_read_u16(twcc->chunks));
	ASSERT_EQ(0x4001, chunk);
	chunk = ntohs(mbuf_read_u16(twcc->chunks));
	ASSERT_EQ(0x2001, chunk);

	ASSERT_EQ(0, (int)mbuf_get_left(twcc->chunks));

out:
	mem_deref(s);
	return err;
}
