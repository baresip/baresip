/**
 * @file mock/nat.c Mock NAT-box
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "../test.h"


#define DEBUG_MODULE "mock/nat"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


enum {
	LAYER_NAT = -1000
};


static struct sa *nat_binding_find(struct nat *nat, uint16_t port)
{
	for (size_t i=0; i<nat->bindingc; i++) {

		if (port == sa_port(&nat->bindingv[i]))
			return &nat->bindingv[i];
	}

	return NULL;
}


static bool nat_helper_send(int *err, struct sa *dst,
			    struct mbuf *mb, void *arg)
{
	struct nat *nat = arg;
	struct sa *cli;
	(void)mb;

	cli = nat_binding_find(nat, sa_port(dst));
	if (!cli) {
		*err = ENOTCONN;
		DEBUG_WARNING("nat: binding to %J not found\n", dst);
		return true;
	}

	*dst = *cli;
	return false;
}


static bool nat_helper_recv(struct sa *src, struct mbuf *mb, void *arg)
{
	struct nat *nat = arg;
	struct sa map;
	(void)mb;

	if (!nat_binding_find(nat, sa_port(src)) &&
	    nat->bindingc < RE_ARRAY_SIZE(nat->bindingv))
		nat->bindingv[nat->bindingc++] = *src;

	map = nat->public_addr;
	sa_set_port(&map, sa_port(src));
	*src = map;

	return false;
}


static void nat_destructor(void *arg)
{
	struct nat *nat = arg;

	mem_deref(nat->uh);
	mem_deref(nat->us);
}


/**
 * Allocate a port-preserving NAT-box on the receiving UDP socket: the
 * source address of every packet received is rewritten to the public
 * address, and replies to it are sent to the real source again.
 *
 * @param natp        Pointer to allocated NAT-box
 * @param us          UDP socket of the receiver
 * @param public_addr Public address of the NAT-box
 *
 * @return 0 if success, otherwise errorcode
 */
int nat_alloc(struct nat **natp, struct udp_sock *us,
	      const struct sa *public_addr)
{
	struct nat *nat;
	int err;

	if (!natp || !us || !public_addr)
		return EINVAL;

	nat = mem_zalloc(sizeof(*nat), nat_destructor);
	if (!nat)
		return ENOMEM;

	nat->public_addr = *public_addr;
	nat->us = mem_ref(us);

	err = udp_register_helper(&nat->uh, us, LAYER_NAT,
				  nat_helper_send, nat_helper_recv, nat);
	if (err)
		mem_deref(nat);
	else
		*natp = nat;

	return err;
}
