/**
 * @file turn.c TURN Module
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup turn turn
 *
 * Traversal Using Relays around NAT (TURN) for media NAT traversal
 */


enum {LAYER = 0, LAYER_APP = 10};
enum {COMPC = 2};


struct mnat_sess {
	struct list medial;
	struct sa srv;
	struct stun_dns *dnsq;
	char *user;
	char *pass;
	mnat_estab_h *estabh;
	void *arg;
	int mediac;
	int proto;
	bool secure;
};


struct mnat_media {
	struct le le;
	struct mnat_sess *sess;
	struct sdp_media *sdpm;

	struct comp {
		struct mnat_media *m;         /* pointer to parent */
		struct sa addr;
		struct turnc *turnc;
		struct udp_sock *sock;
		struct udp_helper *uh_app;
		struct tcp_conn *tc;
		struct tls_conn *tlsc;
		struct mbuf *mb;
		unsigned ix;
	} compv[COMPC];
};


static void session_destructor(void *arg)
{
	struct mnat_sess *sess = arg;

	list_flush(&sess->medial);
	mem_deref(sess->dnsq);
	mem_deref(sess->user);
	mem_deref(sess->pass);
}


static void media_destructor(void *arg)
{
	struct mnat_media *m = arg;
	unsigned i;

	list_unlink(&m->le);
	mem_deref(m->sdpm);

	for (i=0; i<COMPC; i++) {
		struct comp *comp = &m->compv[i];

		mem_deref(comp->uh_app);
		mem_deref(comp->turnc);
		mem_deref(comp->sock);
		mem_deref(comp->tlsc);
		mem_deref(comp->tc);
		mem_deref(comp->mb);
	}
}


static void data_handler(struct comp *comp, const struct sa *src,
			 struct mbuf *mb_pkt)
{
       struct mbuf *mb = mbuf_alloc(mbuf_get_left(mb_pkt));
       if (!mb)
               return;

       /* mbuf cloning is needed due to jitter buffer */

       mbuf_write_mem(mb, mbuf_buf(mb_pkt), mbuf_get_left(mb_pkt));
       mb->pos = 0;

       /* inject packet into UDP socket */
       udp_recv_helper(comp->sock, src, mb, comp->uh_app);

       mem_deref(mb);
}


static void tcp_recv_handler(struct mbuf *mb_pkt, void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->m;
	int err = 0;

	/* re-assembly of fragments */
	if (comp->mb) {
		size_t pos;

		pos = comp->mb->pos;

		comp->mb->pos = comp->mb->end;

		err = mbuf_write_mem(comp->mb,
				     mbuf_buf(mb_pkt), mbuf_get_left(mb_pkt));
		if (err)
			goto out;

		comp->mb->pos = pos;
	}
	else {
		comp->mb = mem_ref(mb_pkt);
	}

	for (;;) {

		size_t len, pos, end;
		struct sa src;
		uint16_t typ;

		if (mbuf_get_left(comp->mb) < 4)
			break;

		typ = ntohs(mbuf_read_u16(comp->mb));
		len = ntohs(mbuf_read_u16(comp->mb));

		if (typ < 0x4000)
			len += STUN_HEADER_SIZE;
		else if (typ < 0x8000)
			len += 4;
		else {
			err = EBADMSG;
			goto out;
		}

		comp->mb->pos -= 4;

		if (mbuf_get_left(comp->mb) < len)
			break;

		pos = comp->mb->pos;
		end = comp->mb->end;

		comp->mb->end = pos + len;

		/* forward packet to TURN client */
		err = turnc_recv(comp->turnc, &src, comp->mb);
		if (err)
			goto out;

		if (mbuf_get_left(comp->mb)) {
			data_handler(comp, &src, comp->mb);
		}

		/* 4 byte alignment */
		while (len & 0x03)
			++len;

		comp->mb->pos = pos + len;
		comp->mb->end = end;

		if (comp->mb->pos >= comp->mb->end) {
			comp->mb = mem_deref(comp->mb);
			break;
		}
	}

 out:
	if (err) {
		m->sess->estabh(err, 0, NULL, m->sess->arg);
	}
}


static void turn_handler(int err, uint16_t scode, const char *reason,
			 const struct sa *relay_addr,
			 const struct sa *mapped_addr,
			 const struct stun_msg *msg,
			 void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->m;
	(void)mapped_addr;
	(void)msg;

	if (!err && !scode) {

		const struct comp *other = &m->compv[comp->ix ^ 1];

		if (comp->ix == 0)
			sdp_media_set_laddr(m->sdpm, relay_addr);
		else
			sdp_media_set_laddr_rtcp(m->sdpm, relay_addr);

		comp->addr = *relay_addr;

		if (other->turnc && !sa_isset(&other->addr, SA_ALL))
			return;

		if (--m->sess->mediac)
			return;
	}

	m->sess->estabh(err, scode, reason, m->sess->arg);
}


static void tcp_estab_handler(void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->m;
	int err;

	info("turn: [%u] %s established for '%s'\n", comp->ix,
	     m->sess->secure ? "TLS" : "TCP",
	     sdp_media_name(m->sdpm));
	err = turnc_alloc(&comp->turnc, NULL, IPPROTO_TCP, comp->tc, 0,
			  &m->sess->srv,
			  m->sess->user, m->sess->pass,
			  TURN_DEFAULT_LIFETIME, turn_handler, comp);
	if (err) {
		m->sess->estabh(err, 0, NULL, m->sess->arg);
	}
}


static void tcp_close_handler(int err, void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->m;

	m->sess->estabh(err ? err : ECONNRESET, 0, NULL, m->sess->arg);
}


static int media_start(struct mnat_sess *sess, struct mnat_media *m)
{
	unsigned i;
	int err = 0;

	for (i=0; i<COMPC; i++) {

		struct comp *comp = &m->compv[i];

		if (!comp->sock)
			continue;

		switch (sess->proto) {

		case IPPROTO_UDP:
			err |= turnc_alloc(&comp->turnc, NULL,
					   IPPROTO_UDP, comp->sock, LAYER,
					   &sess->srv, sess->user, sess->pass,
					   TURN_DEFAULT_LIFETIME,
					   turn_handler, comp);
			break;

		case IPPROTO_TCP:
			err = tcp_connect(&comp->tc, &sess->srv,
					  tcp_estab_handler, tcp_recv_handler,
					  tcp_close_handler, comp);
			if (err)
				break;
#ifdef USE_TLS
			if (sess->secure) {
				struct tls *tls = uag_tls();

				err = tls_start_tcp(&comp->tlsc, tls,
						    comp->tc, 0);
				if (err)
					break;
			}
#endif
			break;

		default:
			return EPROTONOSUPPORT;
		}
	}

	return err;
}


static void dns_handler(int err, const struct sa *srv, void *arg)
{
	struct mnat_sess *sess = arg;
	struct le *le;

	if (err)
		goto out;

	sess->srv = *srv;

	for (le=sess->medial.head; le; le=le->next) {

		struct mnat_media *m = le->data;

		err = media_start(sess, m);
		if (err)
			goto out;
	}

	return;

 out:
	sess->estabh(err, 0, NULL, sess->arg);
}


/* dst contains RTP packet -- [RTP Hdr].[Payload] */
static bool send_handler(int *err, struct sa *dst, struct mbuf *mb, void *arg)
{
       struct comp *comp = arg;

       /* relay packet via TURN over TCP */
       *err = turnc_send(comp->turnc, dst, mb);

       return true;
}


static int session_alloc(struct mnat_sess **sessp,
			 const struct mnat *mnat, struct dnsc *dnsc,
			 int af, const struct stun_uri *srv,
			 const char *user, const char *pass,
			 struct sdp_session *ss, bool offerer,
			 mnat_estab_h *estabh, void *arg)
{
	const char *stun_proto, *stun_usage;
	struct mnat_sess *sess;
	int err;
	(void)mnat;
	(void)ss;
	(void)offerer;

	if (!sessp || !dnsc || !srv || !user || !pass || !ss || !estabh)
		return EINVAL;

	debug("turn: session: %H\n", stunuri_print, srv);

	switch (srv->scheme) {

	case STUN_SCHEME_TURN:
		stun_usage = stun_usage_relay;
		break;

	case STUN_SCHEME_TURNS:
		stun_usage = stuns_usage_relay;
		break;

	default:
		return ENOTSUP;
	}

	switch (srv->proto) {

	case IPPROTO_UDP:
		stun_proto = stun_proto_udp;
		break;

	case IPPROTO_TCP:
		stun_proto = stun_proto_tcp;
		break;

	default:
		return EPROTONOSUPPORT;
	}

	sess = mem_zalloc(sizeof(*sess), session_destructor);
	if (!sess)
		return ENOMEM;

	err  = str_dup(&sess->user, user);
	err |= str_dup(&sess->pass, pass);
	if (err)
		goto out;

	sess->proto  = srv->proto;
#ifdef USE_TLS
	sess->secure = srv->scheme == STUN_SCHEME_TURNS;
#endif
	sess->estabh = estabh;
	sess->arg    = arg;

	err = stun_server_discover(&sess->dnsq, dnsc,
				   stun_usage, stun_proto,
				   af, srv->host, srv->port,
				   dns_handler, sess);

 out:
	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


static int media_alloc(struct mnat_media **mp, struct mnat_sess *sess,
		       struct udp_sock *sock1, struct udp_sock *sock2,
		       struct sdp_media *sdpm,
		       mnat_connected_h *connh, void *arg)
{
	struct mnat_media *m;
	unsigned i;
	int err = 0;
	(void)connh;
	(void)arg;

	if (!mp || !sess || !sdpm)
		return EINVAL;

	m = mem_zalloc(sizeof(*m), media_destructor);
	if (!m)
		return ENOMEM;

	list_append(&sess->medial, &m->le, m);
	m->sdpm  = mem_ref(sdpm);
	m->sess  = sess;
	m->compv[0].sock = mem_ref(sock1);
	m->compv[1].sock = mem_ref(sock2);

	for (i=0; i<COMPC; i++) {
		struct comp *comp = &m->compv[i];

		m->compv[i].m = m;
		m->compv[i].ix = i;

		if (comp->sock && sess->proto == IPPROTO_TCP) {

			err = udp_register_helper(&comp->uh_app, comp->sock,
						  LAYER_APP,
						  send_handler, NULL, comp);
			if (err)
				goto out;
		}
	}

	if (sa_isset(&sess->srv, SA_ALL))
		err = media_start(sess, m);

 out:
	if (err)
		mem_deref(m);
	else {
		*mp = m;
		++sess->mediac;
	}

	return err;
}


static int update(struct mnat_sess *sess)
{
	struct le *le;
	int err = 0;

	if (!sess)
		return EINVAL;

	for (le=sess->medial.head; le; le=le->next) {

		struct mnat_media *m = le->data;
		struct sa raddr[2];
		unsigned i;

		raddr[0] = *sdp_media_raddr(m->sdpm);
		sdp_media_raddr_rtcp(m->sdpm, &raddr[1]);

		for (i=0; i<COMPC; i++) {
			struct comp *comp = &m->compv[i];

			if (comp->turnc && sa_isset(&raddr[i], SA_ALL))
				err |= turnc_add_chan(comp->turnc, &raddr[i],
						      NULL, NULL);
		}
	}

	return err;
}


static struct mnat mnat_turn = {
	.id      = "turn",
	.sessh   = session_alloc,
	.mediah  = media_alloc,
	.updateh = update,
};


static int module_init(void)
{
	mnat_register(baresip_mnatl(), &mnat_turn);

	return 0;
}


static int module_close(void)
{
	mnat_unregister(&mnat_turn);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(turn) = {
	"turn",
	"mnat",
	module_init,
	module_close,
};
