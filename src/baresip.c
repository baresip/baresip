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
	struct contacts contacts;
	struct commands commands;
	struct player *player;
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

	err = contact_init(&baresip.contacts);
	if (err)
		return err;

	err = cmd_init(&baresip.commands);
	if (err)
		return err;

	err = play_init(&baresip.player);
	if (err)
		return err;

	return 0;
}


void baresip_close(void)
{
	baresip.player = mem_deref(baresip.player);
	cmd_close(&baresip.commands);
	contact_close(&baresip.contacts);

	baresip.net = mem_deref(baresip.net);
}


struct network *baresip_network(void)
{
	return baresip.net;
}


struct contacts *baresip_contacts(void)
{
	return &baresip.contacts;
}


struct commands *baresip_commands(void)
{
	return &baresip.commands;
}


struct player *baresip_player(void)
{
	return baresip.player;
}
