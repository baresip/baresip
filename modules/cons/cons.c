/**
 * @file cons.c  Socket-based command-line console
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


enum {CONS_PORT = 5555};

struct ui_st {
	struct ui *ui; /* base class */
	struct udp_sock *us;
	struct tcp_sock *ts;
	struct tcp_conn *tc;
	ui_input_h *h;
	void *arg;
};


static struct ui *cons;
static struct ui_st *cons_cur = NULL;  /* allow only one instance */


static int print_handler(const char *p, size_t size, void *arg)
{
	return mbuf_write_mem(arg, (uint8_t *)p, size);
}


static void udp_recv(const struct sa *src, struct mbuf *mb, void *arg)
{
	struct ui_st *st = arg;
	struct mbuf *mbr = mbuf_alloc(64);
	struct re_printf pf;

	pf.vph = print_handler;
	pf.arg = mbr;

	while (mbuf_get_left(mb))
		st->h(mbuf_read_u8(mb), &pf, st->arg);

	mbr->pos = 0;
	(void)udp_send(st->us, src, mbr);

	mem_deref(mbr);
}


static void cons_destructor(void *arg)
{
	struct ui_st *st = arg;

	mem_deref(st->us);
	mem_deref(st->tc);
	mem_deref(st->ts);

	mem_deref(st->ui);

	cons_cur = NULL;
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

		const char key = mbuf_read_u8(mb);

		st->h(key, &pf, st->arg);
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


static int cons_alloc(struct ui_st **stp, struct ui_prm *prm,
		      ui_input_h *h, void *arg)
{
	struct sa local;
	struct ui_st *st;
	int err;

	if (!stp)
		return EINVAL;

	if (cons_cur) {
		*stp = mem_ref(cons_cur);
		return 0;
	}

	st = mem_zalloc(sizeof(*st), cons_destructor);
	if (!st)
		return ENOMEM;

	st->ui  = mem_ref(cons);
	st->h   = h;
	st->arg = arg;

	err = sa_set_str(&local, "0.0.0.0", prm->port ? prm->port : CONS_PORT);
	if (err)
		goto out;
	err = udp_listen(&st->us, &local, udp_recv, st);
	if (err) {
		warning("cons: failed to listen on UDP port %d (%m)\n",
			sa_port(&local), err);
		goto out;
	}

	err = tcp_listen(&st->ts, &local, tcp_conn_handler, st);
	if (err) {
		warning("cons: failed to listen on TCP port %d (%m)\n",
			sa_port(&local), err);
		goto out;
	}

 out:
	if (err)
		mem_deref(st);
	else
		*stp = cons_cur = st;

	return err;
}


static int cons_init(void)
{
	return ui_register(&cons, "cons", cons_alloc, NULL);
}


static int cons_close(void)
{
	cons = mem_deref(cons);
	return 0;
}


const struct mod_export DECL_EXPORTS(cons) = {
	"cons",
	"ui",
	cons_init,
	cons_close
};
