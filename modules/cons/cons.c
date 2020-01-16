/**
 * @file cons.c  Socket-based command-line console
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup cons cons
 *
 * Console User-Interface (UI) using UDP/TCP sockets
 *
 *
 * This module implements a simple console for connecting to Baresip via
 * UDP or TCP-based sockets. You can use programs like telnet or netcat to
 * connect to the command-line interface.
 *
 * Example, with the cons-module listening on default port 5555:
 *
 \verbatim
  $ netcat -u 127.0.0.1 5555
 \endverbatim
 *
 * The following options can be configured:
 *
 \verbatim
  cons_listen     0.0.0.0:5555         # IP-address and port to listen on
 \endverbatim
 */


enum {CONS_PORT = 5555};

struct ui_st {
	struct udp_sock *us;
	struct tcp_sock *ts;
	struct tcp_conn *tc;
	struct sa udp_peer;
};


static struct ui_st *cons = NULL;  /* allow only one instance */


static int print_handler(const char *p, size_t size, void *arg)
{
	return mbuf_write_mem(arg, (uint8_t *)p, size);
}


static void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct ui_st *st = arg;
	struct mbuf *mbr = mbuf_alloc(64);
	struct re_printf pf;

	st->udp_peer = *src;

	pf.vph = print_handler;
	pf.arg = mbr;

	while (mbuf_get_left(mb)) {
		char ch = mbuf_read_u8(mb);

		if (ch == '\r')
			ch = '\n';

		ui_input_key(baresip_uis(), ch, &pf);
	}

	if (mbr->end > 0) {
		mbr->pos = 0;
		(void)udp_send(st->us, src, mbr);
	}

	mem_deref(mbr);
}


static void cons_destructor(void *arg)
{
	struct ui_st *st = arg;

	mem_deref(st->us);
	mem_deref(st->tc);
	mem_deref(st->ts);
}


static int tcp_write_handler(const char *p, size_t size, void *arg)
{
	struct mbuf mb;

	mb.buf = (uint8_t *)p;
	mb.pos = 0;
	mb.end = mb.size = size;

	return tcp_send(arg, &mb);
}


static void tcp_recv_handler(struct mbuf *mb, void *arg)
{
	struct ui_st *st = arg;
	struct re_printf pf;

	pf.vph = tcp_write_handler;
	pf.arg = st->tc;

	while (mbuf_get_left(mb) > 0) {

		char ch = mbuf_read_u8(mb);

		if (ch == '\r')
			ch = '\n';

		ui_input_key(baresip_uis(), ch, &pf);
	}
}


static void tcp_close_handler(int err, void *arg)
{
	struct ui_st *st = arg;

	(void)err;

	st->tc = mem_deref(st->tc);
}


static void tcp_conn_handler(const struct sa *peer, void *arg)
{
	struct ui_st *st = arg;

	(void)peer;

	/* only one connection allowed */
	st->tc = mem_deref(st->tc);
	(void)tcp_accept(&st->tc, st->ts, NULL, tcp_recv_handler,
			 tcp_close_handler, st);
}


static int cons_alloc(struct ui_st **stp, const struct sa *laddr)
{
	struct ui_st *st;
	int err;

	if (!stp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), cons_destructor);
	if (!st)
		return ENOMEM;

	err = udp_listen(&st->us, laddr, udp_recv, st);
	if (err) {
		warning("cons: failed to listen on UDP %J (%m)\n",
			laddr, err);
		goto out;
	}

	err = tcp_listen(&st->ts, laddr, tcp_conn_handler, st);
	if (err) {
		warning("cons: failed to listen on TCP %J (%m)\n",
			laddr, err);
		goto out;
	}

	debug("cons: UI console listening on %J\n", laddr);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int output_handler(const char *str)
{
	struct mbuf *mb;
	int err = 0;

	if (!str)
		return EINVAL;

	mb = mbuf_alloc(256);
	if (!mb)
		return ENOMEM;

	mbuf_write_str(mb, str);

	if (sa_isset(&cons->udp_peer, SA_ALL)) {
		mb->pos = 0;
		err |= udp_send(cons->us, &cons->udp_peer, mb);
	}

	if (cons->tc) {
		mb->pos = 0;
		err |= tcp_send(cons->tc, mb);
	}

	mem_deref(mb);

	return err;
}


/*
 * Relay log-messages to all active UDP/TCP connections
 */
static void log_handler(uint32_t level, const char *msg)
{
	(void)level;

	output_handler(msg);
}


static struct ui ui_cons = {
	.name    = "cons",
	.outputh = output_handler
};


static struct log lg = {
	.h = log_handler,
};


static int cons_init(void)
{
	struct sa laddr;
	int err;

	if (conf_get_sa(conf_cur(), "cons_listen", &laddr)) {
		sa_set_str(&laddr, "0.0.0.0", CONS_PORT);
	}

	err = cons_alloc(&cons, &laddr);
	if (err)
		return err;

	ui_register(baresip_uis(), &ui_cons);

	log_register_handler(&lg);

	return 0;
}


static int cons_close(void)
{
	log_unregister_handler(&lg);

	ui_unregister(&ui_cons);
	cons = mem_deref(cons);
	return 0;
}


const struct mod_export DECL_EXPORTS(cons) = {
	"cons",
	"ui",
	cons_init,
	cons_close
};
