/**
 * @file test/twcc.c TWCC Testcode
 *
 * Copyright (C) 2025 Sebastian Reimers
 */
#include <re.h>
#include <baresip.h>
#include "test.h"


int test_twcc(void)
{
	struct twcc_status *s = NULL;
	uint16_t chunk;
	struct twcc *twcc;
	uint32_t i;

	int err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	/* --- Packet 1 --- */

	for (i = 0; i < 4; i++) {
		twcc_status_append(s, i, i + 1);
	}
	twcc_status_append(s, 13, 13);
	twcc_status_append(s, 12, 12); /* test double late */

	twcc_status_send_feedback(s);

	twcc = twcc_status_msg(s);
	ASSERT_EQ(2, (int)mbuf_get_left(twcc->chunks));
	chunk  = ntohs(mbuf_read_u16(twcc->chunks));
	ASSERT_EQ(0xbc01, chunk);
	ASSERT_EQ(0, (int)mbuf_get_left(twcc->chunks));

	ASSERT_EQ(5, (int)mbuf_get_left(twcc->deltas));
	uint16_t delta;
	for (i = 0; i < 4; i++) {
		delta = mbuf_read_u8(twcc->deltas);
		ASSERT_EQ(4, delta);
	}
	delta = mbuf_read_u8(twcc->deltas);
	ASSERT_EQ(36, delta);
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
