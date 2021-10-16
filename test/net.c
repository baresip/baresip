/**
 * @file test/net.c  Baresip selftest -- networking
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


static struct config_net default_config = {
	.af = AF_INET
};


int test_network(void)
{
	struct network *net = NULL;
	int err;

	err = net_alloc(&net, &default_config);
	TEST_ERR(err);
	ASSERT_TRUE(net != NULL);

	ASSERT_TRUE( net_af_enabled(net, AF_INET));
	ASSERT_TRUE(!net_af_enabled(net, AF_INET6));

 out:
	mem_deref(net);
	return err;
}
