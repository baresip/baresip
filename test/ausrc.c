/**
 * @file test/ausrc.c  Tests for audio source
 *
 * Copyright (C) 2026 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "ausrc"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


int test_ausrc(void)
{
	struct ausrc_prm prm = { .duration = 0 };
	char device[256] = "";

	int err = module_load(".", "aufile");
	TEST_ERR(err);

	re_snprintf(device, sizeof(device),
		    "%s/wav/square_500Hz_0.8.wav", test_datapath());

	err = ausrc_info(baresip_ausrcl(), "aufile", &prm, device);
	TEST_ERR(err);

	ASSERT_TRUE(prm.duration > 0);

 out:
	module_unload("aufile");

	return err;
}
