/**
 * @file sip_server.c  Selftest for Baresip core -- SIP Server
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


static void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct sip_server *srv = arg;
	struct sip_msg *msg;
	int err;

#if 0
	re_printf("sip: %zu bytes from %J\n", mbuf_get_left(mb), src);
	re_printf("%b\n", mb->buf, mb->end);
#endif

	err = sip_msg_decode(&msg, mb);
	if (err) {
		warning("selftest: sip_msg_decode: %m\n", err);
		return;
	}

	if (0 == pl_strcmp(&msg->met, "REGISTER"))
		srv->got_register_req = true;

	msg->sock = mem_ref(srv->us);
	msg->src  = *src;
	msg->dst  = srv->laddr;
	msg->tp   = SIP_TRANSP_UDP;

	if (srv->terminate)
		err = sip_reply(srv->sip, msg, 503, "Server Error");
	else
		err = sip_reply(srv->sip, msg, 200, "OK");
	if (err) {
		warning("selftest: could not reply: %m\n", err);
	}

	mem_deref(msg);

	if (srv->terminate)
		re_cancel();
}


static void destructor(void *arg)
{
	struct sip_server *srv = arg;

	sip_close(srv->sip, false);
	mem_deref(srv->sip);

	mem_deref(srv->us);
}


int sip_server_create(struct sip_server **srvp)
{
	struct sip_server *srv;
	struct sa laddr_tp;
	int err;

	srv = mem_zalloc(sizeof *srv, destructor);
	if (!srv)
		return ENOMEM;

	err  = sa_set_str(&laddr_tp, "127.0.0.1", 0);
	err |= sa_set_str(&srv->laddr, "127.0.0.1", 0);
	if (err)
		goto out;

	err = sip_alloc(&srv->sip, NULL, 16, 16, 16,
			"dummy SIP registrar", NULL, NULL);
	if (err)
		goto out;

	err = sip_transp_add(srv->sip, SIP_TRANSP_UDP, &laddr_tp);
	if (err) {
		warning("failed to add sip transport (%m)\n", err);
		goto out;
	}

	err = udp_listen(&srv->us, &srv->laddr, udp_recv, srv);
	if (err) {
		warning("sip: udp_listen on '%J' failed (%d/%m)\n",
			&srv->laddr, err, err);
		goto out;
	}

	err = udp_local_get(srv->us, &srv->laddr);
	if (err) {
		warning("sip: udp_local_get\n");
		goto out;
	}

#if 0
	re_printf("sip: listen on %J\n", &srv->laddr);
#endif

 out:
	if (err)
		mem_deref(srv);
	else
		*srvp = srv;

	return err;
}
