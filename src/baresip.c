/**
 * @file baresip.c Top-level baresip struct
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/*
 * Top-level struct that holds all other subsystems
 * (move this instance to main.c later)
 */
static struct baresip {
	struct network *net;

} baresip;


int baresip_init(struct config *cfg, bool prefer_ipv6)
{
	int err;

	if (!cfg)
		return EINVAL;

	baresip.net = mem_deref(baresip.net);

	/* Initialise Network */
	err = net_alloc(&baresip.net, &cfg->net,
			prefer_ipv6 ? AF_INET6 : AF_INET);
	if (err) {
		warning("ua: network init failed: %m\n", err);
		return err;
	}

	return 0;
}


void baresip_close(void)
{
	baresip.net = mem_deref(baresip.net);
}


struct network *baresip_network(void)
{
	return baresip.net;
}
