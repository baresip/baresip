/**
 * @file test/net.c  Baresip selftest -- networking
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


static struct config_net default_config;


static void net_change_handler(void *arg)
{
	unsigned *count = arg;
	++*count;
	info("network changed\n");
}


int test_network(void)
{
	struct network *net = NULL;
	unsigned change_count = 0;
	int err;

	err = net_alloc(&net, &default_config, AF_INET);
	TEST_ERR(err);
	ASSERT_TRUE(net != NULL);

	ASSERT_EQ(AF_INET, net_af(net));

	net_change(net, 1, net_change_handler, &change_count);

	ASSERT_EQ(0, change_count);

	net_force_change(net);

	ASSERT_EQ(1, change_count);

 out:
	mem_deref(net);
	return err;
}
