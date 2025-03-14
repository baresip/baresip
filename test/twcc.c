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

	int err = twcc_status_alloc(&s, NULL);
	TEST_ERR(err);

	twcc_status_append(s, 0);
	twcc_status_append(s, 1);
	twcc_status_append(s, 2);
	/* lost 2 packets */
	twcc_status_append(s, 5);

	twcc_status_send_feedback(s);

	struct mbuf *chunks = twcc_status_chunks(s);
	struct mbuf *delays = twcc_status_delays(s);


#if 0
	for (uint32_t i = 0; i < UINT16_MAX; i++)
	{
		twcc_status_append(s, i);
	}

	for (uint32_t i = 5; i < 16; i++)
	{
		twcc_status_append(s, i);
	}
#endif

out:
	mem_deref(s);
	return err;
}
