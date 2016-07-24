/**
 * @file listener.c Port Control Protocol module -- multicast listener
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */

#include <re.h>
#include <rew.h>
#include <baresip.h>
#include "pcp.h"


/*
 * Listen for incoming notifications on unicast/multicast port 5350
 */


struct pcp_listener {
	struct udp_sock *us;
	struct sa srv;
	struct sa group;
	pcp_msg_h *msgh;
	void *arg;
};


static void destructor(void *arg)
{
	struct pcp_listener *pl = arg;

	if (sa_isset(&pl->group, SA_ADDR))
		(void)udp_multicast_leave(pl->us, &pl->group);

	mem_deref(pl->us);
}


static void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct pcp_listener *pl = arg;
	struct pcp_msg *msg;
	int err;

#if 0
	if (!sa_cmp(src, &pl->srv, SA_ADDR)) {
		debug("pcp: listener: ignore %zu bytes from non-server %J\n",
		      mb->end, src);
		return;
	}
#endif

	err = pcp_msg_decode(&msg, mb);
	if (err)
		return;

	/* Validate PCP request */
	if (!msg->hdr.resp) {
		info("pcp: listener: ignore request from %J\n", src);
		goto out;
	}

	if (pl->msgh)
		pl->msgh(msg, pl->arg);

 out:
	mem_deref(msg);
}


int pcp_listen(struct pcp_listener **plp, const struct sa *srv,
	       pcp_msg_h *msgh, void *arg)
{
	struct pcp_listener *pl;
	struct sa laddr;
	int err;

	if (!plp || !srv || !msgh)
		return EINVAL;

	pl = mem_zalloc(sizeof(*pl), destructor);
	if (!pl)
		return ENOMEM;

	pl->srv  = *srv;
	pl->msgh = msgh;
	pl->arg  = arg;

	/* note: must listen on ANY to get multicast working */
	sa_init(&laddr, sa_af(srv));
	sa_set_port(&laddr, PCP_PORT_CLI);

	err = udp_listen(&pl->us, &laddr, udp_recv, pl);
	if (err)
		goto out;

	switch (sa_af(&laddr)) {

	case AF_INET:
		err = sa_set_str(&pl->group, "224.0.0.1", 0);
		break;

	case AF_INET6:
		err = sa_set_str(&pl->group, "ff02::1", 0);
		break;

	default:
		err = EAFNOSUPPORT;
		break;
	}
	if (err)
		goto out;

	err = udp_multicast_join(pl->us, &pl->group);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(pl);
	else
		*plp = pl;

	return err;
}
