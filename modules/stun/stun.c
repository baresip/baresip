/**
 * @file stun.c STUN Module for Media NAT-traversal
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup stun stun
 *
 * Session Traversal Utilities for NAT (STUN) for media NAT traversal
 */


enum {LAYER = 0, INTERVAL = 30};

struct mnat_sess {
	struct list medial;
	struct sa srv;
	struct stun_dns *dnsq;
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
	struct stun_keepalive *ska1;
	struct stun_keepalive *ska2;
	void *sock1;
	void *sock2;
};


static void session_destructor(void *arg)
{
	struct mnat_sess *sess = arg;

	list_flush(&sess->medial);
	mem_deref(sess->dnsq);
}


static void media_destructor(void *arg)
{
	struct mnat_media *m = arg;

	list_unlink(&m->le);
	mem_deref(m->sdpm);
	mem_deref(m->ska1);
	mem_deref(m->ska2);
	mem_deref(m->sock1);
	mem_deref(m->sock2);
}


static void mapped_handler1(int err, const struct sa *map_addr, void *arg)
{
	struct mnat_media *m = arg;

	if (!err) {

		sdp_media_set_laddr(m->sdpm, map_addr);

		m->addr1 = *map_addr;

		if (m->ska2 && !sa_isset(&m->addr2, SA_ALL))
			return;

		if (--m->sess->mediac)
			return;
	}

	m->sess->estabh(err, 0, NULL, m->sess->arg);
}


static void mapped_handler2(int err, const struct sa *map_addr, void *arg)
{
	struct mnat_media *m = arg;

	if (!err) {

		sdp_media_set_laddr_rtcp(m->sdpm, map_addr);

		m->addr2 = *map_addr;

		if (m->ska1 && !sa_isset(&m->addr1, SA_ALL))
			return;

		if (--m->sess->mediac)
			return;
	}

	m->sess->estabh(err, 0, NULL, m->sess->arg);
}


static int media_start(struct mnat_sess *sess, struct mnat_media *m)
{
	int err = 0;

	if (m->sock1) {
		err |= stun_keepalive_alloc(&m->ska1, IPPROTO_UDP,
					    m->sock1, LAYER, &sess->srv, NULL,
					    mapped_handler1, m);
	}
	if (m->sock2) {
		err |= stun_keepalive_alloc(&m->ska2, IPPROTO_UDP,
					    m->sock2, LAYER, &sess->srv, NULL,
					    mapped_handler2, m);
	}
	if (err)
		return err;

	stun_keepalive_enable(m->ska1, INTERVAL);
	stun_keepalive_enable(m->ska2, INTERVAL);

	return 0;
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
	(void)user;
	(void)pass;
	(void)ss;
	(void)offerer;

	if (!sessp || !dnsc || !srv || !ss || !estabh)
		return EINVAL;

	if (srv->scheme != STUN_SCHEME_STUN)
		return ENOTSUP;

	sess = mem_zalloc(sizeof(*sess), session_destructor);
	if (!sess)
		return ENOMEM;

	sess->estabh = estabh;
	sess->arg    = arg;

	err = stun_server_discover(&sess->dnsq, dnsc,
				   stun_usage_binding, stun_proto_udp,
				   af, srv->host, srv->port,
				   dns_handler, sess);

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


static struct mnat mnat_stun = {
	.id      = "stun",
	.sessh   = session_alloc,
	.mediah  = media_alloc,
};


static int module_init(void)
{
	mnat_register(baresip_mnatl(), &mnat_stun);

	return 0;
}


static int module_close(void)
{
	mnat_unregister(&mnat_stun);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(stun) = {
	"stun",
	"mnat",
	module_init,
	module_close,
};
