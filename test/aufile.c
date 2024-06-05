/**
 * @file test/aufile.c  Baresip selftest -- module/aufile
 *
 * Copyright (C) 2024 commend.com - Christian Spielberger
 */
#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "test.h"


int test_aufile_duration(void)
{
	struct ausrc_st *ausrc = NULL;
	struct ausrc_prm prm;
	int err = 0;

	err = module_load(".", "aufile");
	TEST_ERR(err);

	ASSERT_EQ(0, err);

	err = ausrc_alloc(&ausrc, baresip_ausrcl(), "aufile",
			  &prm, "../share/message.wav", NULL, NULL, NULL);
	TEST_ERR(err);
	ASSERT_EQ(787, prm.duration);
	ASSERT_EQ(1, prm.ch);
	ASSERT_EQ(8000, prm.srate);
	ASSERT_EQ(AUFMT_S16LE, prm.fmt);
	ASSERT_EQ(0, prm.ptime);
out:
	mem_deref(ausrc);
	module_unload("aufile");
	return err;
}
