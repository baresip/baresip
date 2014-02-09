/**
 * @file natpmp.c NAT-PMP Module for Media NAT-traversal
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "libnatpmp.h"


/**
 * @defgroup natpmp natpmp
 *
 * NAT Port Mapping Protocol (NAT-PMP)
 *
 * https://tools.ietf.org/html/rfc6886
 */

enum {
	LIFETIME = 300 /* seconds */
};

struct mnat_sess {
	struct list medial;
	mnat_estab_h *estabh;
	void *arg;
};

struct mnat_media {
	struct le le;
	struct mnat_sess *sess;
	struct sdp_media *sdpm;
	struct natpmp_req *natpmp;
	struct tmr tmr;
	uint16_t int_port;
	uint32_t lifetime;
	bool granted;
};


static struct mnat *mnat;
static struct sa natpmp_srv, natpmp_extaddr;
static struct natpmp_req *natpmp_ext;


static void natpmp_resp_handler(int err, const struct natpmp_resp *resp,
				void *arg);


static void session_destructor(void *arg)
{
	struct mnat_sess *sess = arg;

	list_flush(&sess->medial);
}


static void media_destructor(void *arg)
{
	struct mnat_media *m = arg;

	/* Destroy the mapping */
	if (m->granted) {
		(void)natpmp_mapping_request(NULL, &natpmp_srv,
					     m->int_port, 0, 0, NULL, NULL);
	}

	list_unlink(&m->le);
	tmr_cancel(&m->tmr);
	mem_deref(m->sdpm);
	mem_deref(m->natpmp);
}


static void is_complete(struct mnat_sess *sess)
{
	struct le *le;

	for (le = sess->medial.head; le; le = le->next) {

		struct mnat_media *m = le->data;

		if (!m->granted)
			return;
	}

	if (sess->estabh) {
		sess->estabh(0, 0, "done", sess->arg);

		sess->estabh = NULL;
	}
}


static void refresh_timeout(void *arg)
{
	struct mnat_media *m = arg;

	m->natpmp = mem_deref(m->natpmp);
	(void)natpmp_mapping_request(&m->natpmp, &natpmp_srv,
				     m->int_port, 0, m->lifetime,
				     natpmp_resp_handler, m);
}


static void natpmp_resp_handler(int err, const struct natpmp_resp *resp,
				void *arg)
{
	struct mnat_media *m = arg;
	struct sa map_addr;

	if (err) {
		warning("natpmp: response error: %m\n", err);
		return;
	}

	if (resp->op != NATPMP_OP_MAPPING_UDP)
		return;
	if (resp->result != NATPMP_SUCCESS) {
		warning("natpmp: request failed with result code: %d\n",
			resp->result);
		return;
	}

	if (resp->u.map.int_port != m->int_port) {
		info("natpmp: ignoring response for internal_port=%u\n",
		     resp->u.map.int_port);
		return;
	}

	info("natpmp: mapping granted:"
	     " internal_port=%u, external_port=%u, lifetime=%u\n",
	     resp->u.map.int_port, resp->u.map.ext_port,
	     resp->u.map.lifetime);

	map_addr = natpmp_extaddr;
	sa_set_port(&map_addr, resp->u.map.ext_port);
	m->lifetime = resp->u.map.lifetime;

	/* Update SDP media with external IP-address mapping */
	sdp_media_set_laddr(m->sdpm, &map_addr);

	m->granted = true;

	tmr_start(&m->tmr, m->lifetime * 1000 * 3/4, refresh_timeout, m);

	is_complete(m->sess);
}


static int session_alloc(struct mnat_sess **sessp, struct dnsc *dnsc,
			 int af, const char *srv, uint16_t port,
			 const char *user, const char *pass,
			 struct sdp_session *ss, bool offerer,
			 mnat_estab_h *estabh, void *arg)
{
	struct mnat_sess *sess;
	int err = 0;
	(void)af;
	(void)port;
	(void)user;
	(void)pass;
	(void)ss;
	(void)offerer;

	if (!sessp || !dnsc || !srv || !ss || !estabh)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), session_destructor);
	if (!sess)
		return ENOMEM;

	sess->estabh = estabh;
	sess->arg    = arg;

	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


static int media_alloc(struct mnat_media **mp, struct mnat_sess *sess,
		       int proto, void *sock1, void *sock2,
		       struct sdp_media *sdpm)
{
	struct mnat_media *m;
	struct sa laddr;
	int err = 0;
	(void)sock2;

	if (!mp || !sess || !sdpm || proto != IPPROTO_UDP)
		return EINVAL;

	m = mem_zalloc(sizeof(*m), media_destructor);
	if (!m)
		return ENOMEM;

	list_append(&sess->medial, &m->le, m);
	m->sess = sess;
	m->sdpm = mem_ref(sdpm);
	m->lifetime = LIFETIME;

	err = udp_local_get(sock1, &laddr);
	if (err)
		goto out;

	m->int_port = sa_port(&laddr);

	info("natpmp: local UDP port is %u\n", m->int_port);

	err = natpmp_mapping_request(&m->natpmp, &natpmp_srv,
				     m->int_port, 0, m->lifetime,
				     natpmp_resp_handler, m);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(m);
	else {
		*mp = m;
	}

	return err;
}


static void extaddr_handler(int err, const struct natpmp_resp *resp, void *arg)
{
	(void)arg;

	if (err) {
		warning("natpmp: external address ERROR: %m\n", err);
		return;
	}

	if (resp->result != NATPMP_SUCCESS) {
		warning("natpmp: external address failed"
			" with result code: %d\n", resp->result);
		return;
	}

	if (resp->op != NATPMP_OP_EXTERNAL)
		return;

	sa_set_in(&natpmp_extaddr, resp->u.ext_addr, 0);

	info("natpmp: discovered External address: %j\n", &natpmp_extaddr);
}


static bool net_rt_handler(const char *ifname, const struct sa *dst,
			   int dstlen, const struct sa *gw, void *arg)
{
	(void)dstlen;
	(void)arg;

	if (sa_af(dst) != AF_INET)
		return false;

	if (sa_in(dst) == 0) {
		natpmp_srv = *gw;
		sa_set_port(&natpmp_srv, NATPMP_PORT);
		info("natpmp: found default gateway %j on interface '%s'\n",
		     gw, ifname);
		return true;
	}

	return false;
}


static int module_init(void)
{
	int err;

	sa_init(&natpmp_srv, AF_INET);
	sa_set_port(&natpmp_srv, NATPMP_PORT);

	net_rt_list(net_rt_handler, NULL);

	conf_get_sa(conf_cur(), "natpmp_server", &natpmp_srv);

	info("natpmp: using NAT-PMP server at %J\n", &natpmp_srv);

	err = natpmp_external_request(&natpmp_ext, &natpmp_srv,
				      extaddr_handler, NULL);
	if (err)
		return err;

	return mnat_register(&mnat, "natpmp", NULL,
			     session_alloc, media_alloc, NULL);
}


static int module_close(void)
{
	mnat       = mem_deref(mnat);
	natpmp_ext = mem_deref(natpmp_ext);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(natpmp) = {
	"natpmp",
	"mnat",
	module_init,
	module_close,
};
