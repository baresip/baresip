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


struct nat_tcp_conn {
	struct le le;
	struct tcp_conn *in;
	struct tcp_conn *out;
	struct mbuf *pending;
	bool estab;
};


static void nat_tcp_conn_destructor(void *arg)
{
	struct nat_tcp_conn *conn = arg;

	list_unlink(&conn->le);
	mem_deref(conn->in);
	mem_deref(conn->out);
	mem_deref(conn->pending);
}


static void nat_tcp_close_handler(int err, void *arg)
{
	struct nat_tcp_conn *conn = arg;
	(void)err;

	mem_deref(conn);
}


static void nat_tcp_out_estab_handler(void *arg)
{
	struct nat_tcp_conn *conn = arg;

	conn->estab = true;

	if (conn->pending) {
		conn->pending->pos = 0;
		(void)tcp_send(conn->out, conn->pending);
		conn->pending = mem_deref(conn->pending);
	}
}


static void nat_tcp_out_recv_handler(struct mbuf *mb, void *arg)
{
	struct nat_tcp_conn *conn = arg;

	(void)tcp_send(conn->in, mb);
}


static void nat_tcp_in_recv_handler(struct mbuf *mb, void *arg)
{
	struct nat_tcp_conn *conn = arg;

	if (conn->estab) {
		(void)tcp_send(conn->out, mb);
		return;
	}

	if (!conn->pending)
		conn->pending = mbuf_alloc(mbuf_get_left(mb));

	if (conn->pending)
		(void)mbuf_write_mem(conn->pending, mbuf_buf(mb),
				     mbuf_get_left(mb));
}


static void nat_tcp_conn_handler(const struct sa *peer, void *arg)
{
	struct nat_tcp *nat = arg;
	struct nat_tcp_conn *conn;
	int err;
	(void)peer;

	conn = mem_zalloc(sizeof(*conn), nat_tcp_conn_destructor);
	if (!conn) {
		tcp_reject(nat->ts);
		return;
	}

	list_append(&nat->connl, &conn->le, conn);

	err  = tcp_accept(&conn->in, nat->ts, NULL, nat_tcp_in_recv_handler,
			  nat_tcp_close_handler, conn);
	err |= tcp_connect(&conn->out, &nat->dst, nat_tcp_out_estab_handler,
			   nat_tcp_out_recv_handler, nat_tcp_close_handler,
			   conn);
	if (err) {
		DEBUG_WARNING("nat: could not relay connection (%m)\n", err);
		mem_deref(conn);
	}
}


static void nat_tcp_destructor(void *arg)
{
	struct nat_tcp *nat = arg;

	list_flush(&nat->connl);
	mem_deref(nat->ts);
}


/**
 * Allocate a port-translating NAT-box for TCP: every connection accepted
 * on laddr is relayed to dst from a new local port, so dst sees the client
 * at a different address than the client itself uses.
 *
 * @param natp  Pointer to allocated NAT-box
 * @param dst   Address to relay connections to
 *
 * @return 0 if success, otherwise errorcode
 */
int nat_tcp_alloc(struct nat_tcp **natp, const struct sa *dst)
{
	struct nat_tcp *nat;
	int err;

	if (!natp || !dst)
		return EINVAL;

	nat = mem_zalloc(sizeof(*nat), nat_tcp_destructor);
	if (!nat)
		return ENOMEM;

	nat->dst = *dst;

	err = sa_set_str(&nat->laddr, "127.0.0.1", 0);
	if (!err)
		err = tcp_listen(&nat->ts, &nat->laddr, nat_tcp_conn_handler,
				 nat);
	if (!err)
		err = tcp_sock_local_get(nat->ts, &nat->laddr);

	if (err)
		mem_deref(nat);
	else
		*natp = nat;

	return err;
}
