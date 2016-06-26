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
	struct comp {
		struct natpmp_req *natpmp;
		struct mnat_media *media;   /* pointer to parent */
		struct tmr tmr;
		uint16_t int_port;
		uint32_t lifetime;
		unsigned id;
		bool granted;
	} compv[2];
	unsigned compc;

	struct le le;
	struct mnat_sess *sess;
	struct sdp_media *sdpm;
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
	unsigned i;

	list_unlink(&m->le);

	for (i=0; i<m->compc; i++) {
		struct comp *comp = &m->compv[i];

		/* Destroy the mapping */
		if (comp->granted) {
			(void)natpmp_mapping_request(NULL, &natpmp_srv,
						     comp->int_port, 0, 0,
						     NULL, NULL);
		}

		tmr_cancel(&comp->tmr);
		mem_deref(comp->natpmp);
	}

	mem_deref(m->sdpm);
}


static void complete(struct mnat_sess *sess, int err)
{
	mnat_estab_h *estabh = sess->estabh;

	if (sess->estabh) {

		sess->estabh = NULL;

		estabh(err, 0, "done", sess->arg);
	}
}


static bool all_components_granted(const struct mnat_media *m)
{
	unsigned i;

	if (!m || !m->compc)
		return false;

	for (i=0; i<m->compc; i++) {
		const struct comp *comp = &m->compv[i];
		if (!comp->granted)
			return false;
	}

	return true;
}

static void is_complete(struct mnat_sess *sess)
{
	struct le *le;

	for (le = sess->medial.head; le; le = le->next) {

		struct mnat_media *m = le->data;

		if (!all_components_granted(m))
			return;
	}

	complete(sess, 0);
}


static void refresh_timeout(void *arg)
{
	struct comp *comp = arg;

	comp->natpmp = mem_deref(comp->natpmp);
	(void)natpmp_mapping_request(&comp->natpmp, &natpmp_srv,
				     comp->int_port, 0, comp->lifetime,
				     natpmp_resp_handler, comp);
}


static void natpmp_resp_handler(int err, const struct natpmp_resp *resp,
				void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->media;
	struct sa map_addr;

	if (err) {
		warning("natpmp: response error: %m\n", err);
		complete(m->sess, err);
		return;
	}

	if (resp->op != NATPMP_OP_MAPPING_UDP)
		return;
	if (resp->result != NATPMP_SUCCESS) {
		warning("natpmp: request failed with result code: %d\n",
			resp->result);
		complete(m->sess, EPROTO);
		return;
	}

	if (resp->u.map.int_port != comp->int_port) {
		info("natpmp: ignoring response for internal_port=%u\n",
		     resp->u.map.int_port);
		return;
	}

	info("natpmp: mapping granted for comp %u:"
	     " internal_port=%u, external_port=%u, lifetime=%u\n",
	     comp->id,
	     resp->u.map.int_port, resp->u.map.ext_port,
	     resp->u.map.lifetime);

	map_addr = natpmp_extaddr;
	sa_set_port(&map_addr, resp->u.map.ext_port);
	comp->lifetime = resp->u.map.lifetime;

	/* Update SDP media with external IP-address mapping */
	if (comp->id == 1)
		sdp_media_set_laddr(m->sdpm, &map_addr);
	else
		sdp_media_set_laddr_rtcp(m->sdpm, &map_addr);

	comp->granted = true;

	tmr_start(&comp->tmr, comp->lifetime * 1000 * 3/4,
		  refresh_timeout, comp);

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


static int comp_alloc(struct comp *comp, void *sock)
{
	struct sa laddr;
	int err;

	err = udp_local_get(sock, &laddr);
	if (err)
		goto out;

	comp->int_port = sa_port(&laddr);

	info("natpmp: `%s' stream comp %u local UDP port is %u\n",
	     sdp_media_name(comp->media->sdpm), comp->id, comp->int_port);

	err = natpmp_mapping_request(&comp->natpmp, &natpmp_srv,
				     comp->int_port, 0, comp->lifetime,
				     natpmp_resp_handler, comp);
	if (err)
		goto out;

 out:
	return err;
}


static int media_alloc(struct mnat_media **mp, struct mnat_sess *sess,
		       int proto, void *sock1, void *sock2,
		       struct sdp_media *sdpm)
{
	struct mnat_media *m;
	unsigned i;
	int err = 0;
	(void)sock2;

	if (!mp || !sess || !sdpm || proto != IPPROTO_UDP)
		return EINVAL;
	if (!sock1)
		return EINVAL;

	m = mem_zalloc(sizeof(*m), media_destructor);
	if (!m)
		return ENOMEM;

	m->compc = sock2 ? 2 : 1;

	list_append(&sess->medial, &m->le, m);
	m->sess = sess;
	m->sdpm = mem_ref(sdpm);

	for (i=0; i<m->compc; i++) {

		struct comp *comp = &m->compv[i];

		comp->id = i+1;
		comp->media = m;
		comp->lifetime = LIFETIME;

		err = comp_alloc(comp, i==0 ? sock1 : sock2);
		if (err)
			goto out;
	}

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
