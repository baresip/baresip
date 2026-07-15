/**
 * @file test/l16.c  Tests for the L16 codec table
 *
 * Copyright (C) 2026 Rebel
 */
#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "l16"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


int test_l16_ptime(void)
{
	static const struct {
		uint32_t srate;
		uint8_t ch;
		uint32_t ptime;
	} expv[] = {
		/* fixed ptime: MTU guards (payload at 20ms would not
		 * fit a 1500-byte MTU) */
		{48000, 2,  4},
		{44100, 2,  4},
		{32000, 2, 10},
		{48000, 1, 10},
		{44100, 1, 10},
		/* no codec ptime: account/peer ptime applies, global
		 * 20ms default otherwise */
		{16000, 2,  0},
		{ 8000, 2,  0},
		{32000, 1,  0},
		{16000, 1,  0},
		{ 8000, 1,  0},
	};
	const struct aucodec *ac;
	size_t i;
	int err;

	err = module_load(".", "l16");
	TEST_ERR(err);

	for (i=0; i<RE_ARRAY_SIZE(expv); i++) {
		ac = aucodec_find(baresip_aucodecl(), "L16",
				  expv[i].srate, expv[i].ch);
		ASSERT_TRUE(ac != NULL);
		ASSERT_EQ(expv[i].ptime, ac->ptime);
	}

 out:
	module_unload("l16");
	return err;
}
