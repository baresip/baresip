/**
 * @file turn.c TURN Module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup turn turn
 *
 * Traversal Using Relays around NAT (TURN) for media NAT traversal
 */


enum {LAYER = 0};


struct mnat_sess {
	struct list medial;
	struct sa srv;
	struct stun_dns *dnsq;
	char *user;
	char *pass;
	mnat_estab_h *estabh;
	void *arg;
	int mediac;
};


struct mnat_media {
	struct le le;
	struct sa addr1;
	struct sa addr2;
	struct mnat_sess *sess;
	struct sdp_media *sdpm;
	struct turnc *turnc1;
	struct turnc *turnc2;
	void *sock1;
	void *sock2;
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

	list_unlink(&m->le);
	mem_deref(m->sdpm);
	mem_deref(m->turnc1);
	mem_deref(m->turnc2);
	mem_deref(m->sock1);
	mem_deref(m->sock2);
}


static void turn_handler1(int err, uint16_t scode, const char *reason,
			  const struct sa *relay_addr,
			  const struct sa *mapped_addr,
			  const struct stun_msg *msg,
			  void *arg)
{
	struct mnat_media *m = arg;
	(void)mapped_addr;
	(void)msg;

	if (!err && !scode) {

		sdp_media_set_laddr(m->sdpm, relay_addr);

		m->addr1 = *relay_addr;

		if (m->turnc2 && !sa_isset(&m->addr2, SA_ALL))
			return;

		if (--m->sess->mediac)
			return;
	}

	m->sess->estabh(err, scode, reason, m->sess->arg);
}


static void turn_handler2(int err, uint16_t scode, const char *reason,
			  const struct sa *relay_addr,
			  const struct sa *mapped_addr,
			  const struct stun_msg *msg,
			  void *arg)
{
	struct mnat_media *m = arg;
	(void)mapped_addr;
	(void)msg;

	if (!err && !scode) {

		sdp_media_set_laddr_rtcp(m->sdpm, relay_addr);

		m->addr2 = *relay_addr;

		if (m->turnc1 && !sa_isset(&m->addr1, SA_ALL))
			return;

		if (--m->sess->mediac)
			return;
	}

	m->sess->estabh(err, scode, reason, m->sess->arg);
}


static int media_start(struct mnat_sess *sess, struct mnat_media *m)
{
	int err = 0;

	if (m->sock1) {
		err |= turnc_alloc(&m->turnc1, NULL,
				   IPPROTO_UDP, m->sock1, LAYER,
				   &sess->srv, sess->user, sess->pass,
				   TURN_DEFAULT_LIFETIME,
				   turn_handler1, m);
	}
	if (m->sock2) {
		err |= turnc_alloc(&m->turnc2, NULL,
				   IPPROTO_UDP, m->sock2, LAYER,
				   &sess->srv, sess->user, sess->pass,
				   TURN_DEFAULT_LIFETIME,
				   turn_handler2, m);
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


static int session_alloc(struct mnat_sess **sessp,
			 const struct mnat *mnat, struct dnsc *dnsc,
			 int af, const struct stun_uri *srv,
			 const char *user, const char *pass,
			 struct sdp_session *ss, bool offerer,
			 mnat_estab_h *estabh, void *arg)
{
	struct mnat_sess *sess;
	int err;
	(void)mnat;
	(void)ss;
	(void)offerer;

	if (!sessp || !dnsc || !srv || !user || !pass || !ss || !estabh)
		return EINVAL;

	if (srv->scheme != STUN_SCHEME_TURN)
		return ENOTSUP;

	sess = mem_zalloc(sizeof(*sess), session_destructor);
	if (!sess)
		return ENOMEM;

	err  = str_dup(&sess->user, user);
	err |= str_dup(&sess->pass, pass);
	if (err)
		goto out;

	sess->estabh = estabh;
	sess->arg    = arg;

	err = stun_server_discover(&sess->dnsq, dnsc,
				   stun_usage_relay, stun_proto_udp,
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
	m->sock1 = mem_ref(sock1);
	m->sock2 = mem_ref(sock2);

	if (sa_isset(&sess->srv, SA_ALL))
		err = media_start(sess, m);

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
		struct sa raddr1, raddr2;

		raddr1 = *sdp_media_raddr(m->sdpm);
		sdp_media_raddr_rtcp(m->sdpm, &raddr2);

		if (m->turnc1 && sa_isset(&raddr1, SA_ALL))
			err |= turnc_add_chan(m->turnc1, &raddr1, NULL, NULL);

		if (m->turnc2 && sa_isset(&raddr2, SA_ALL))
			err |= turnc_add_chan(m->turnc2, &raddr2, NULL, NULL);
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
