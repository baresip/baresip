/**
 * @file pcp.c Port Control Protocol for Media NAT-traversal
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include <rew.h>
#include <baresip.h>
#include "pcp.h"


/**
 * @defgroup pcp pcp
 *
 * Port Control Protocol (PCP)
 *
 * This module implements the medianat interface with PCP, which is
 * the successor of the NAT-PMP protocol.
 */


enum {
	LIFETIME = 120 /* seconds */
};

struct mnat_sess {
	struct le le;
	struct list medial;
	mnat_estab_h *estabh;
	void *arg;
};

struct mnat_media {

	struct comp {
		struct pcp_request *pcp;
		struct mnat_media *media;  /* pointer to parent */
		unsigned id;
		bool granted;
	} compv[2];
	unsigned compc;

	struct le le;
	struct mnat_sess *sess;
	struct sdp_media *sdpm;
	uint32_t srv_epoch;
};


static struct mnat *mnat;
static struct sa pcp_srv;
static struct list sessl;
static struct pcp_listener *lsnr;


static void session_destructor(void *arg)
{
	struct mnat_sess *sess = arg;

	list_unlink(&sess->le);
	list_flush(&sess->medial);
}


static void media_destructor(void *arg)
{
	struct mnat_media *m = arg;
	unsigned i;

	list_unlink(&m->le);

	for (i=0; i<m->compc; i++) {
		struct comp *comp = &m->compv[i];

		mem_deref(comp->pcp);
	}

	mem_deref(m->sdpm);
}


static void complete(struct mnat_sess *sess, int err, const char *reason)
{
	mnat_estab_h *estabh = sess->estabh;
	void *arg = sess->arg;

	sess->estabh = NULL;

	if (estabh) {
		estabh(err, 0, reason, arg);
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

	complete(sess, 0, "done");
}


/* todo: detect multiple responses */
static void pcp_resp_handler(int err, struct pcp_msg *msg, void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->media;
	const struct pcp_map *map;

	if (err) {
		warning("pcp: mapping error: %m\n", err);

		complete(m->sess, err, NULL);
		return;
	}
	else if (msg->hdr.result != PCP_SUCCESS) {
		warning("pcp: mapping error: %s\n",
			pcp_result_name(msg->hdr.result));

		re_printf("%H\n", pcp_msg_print, msg);

		complete(m->sess, EPROTO, "pcp error");
		return;
	}

	map = pcp_msg_payload(msg);

	info("pcp: %s: mapping for %s:"
	     " internal_port=%u, external_addr=%J\n",
	     sdp_media_name(m->sdpm),
	     comp->id==1 ? "RTP" : "RTCP",
	     map->int_port, &map->ext_addr);

	/* Update SDP media with external IP-address mapping */
	if (comp->id == 1)
		sdp_media_set_laddr(m->sdpm, &map->ext_addr);
	else
		sdp_media_set_laddr_rtcp(m->sdpm, &map->ext_addr);

	comp->granted = true;
	m->srv_epoch = msg->hdr.epoch;

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

	list_append(&sessl, &sess->le, sess);

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
	struct pcp_map map;
	unsigned i;
	int err = 0;

	if (!mp || !sess || !sdpm || proto != IPPROTO_UDP)
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

		err = udp_local_get(i==0 ? sock1 : sock2, &laddr);
		if (err)
			goto out;

		rand_bytes(map.nonce, sizeof(map.nonce));
		map.proto = proto;
		map.int_port = sa_port(&laddr);
		/* note: using same address-family as the PCP server */
		sa_init(&map.ext_addr, sa_af(&pcp_srv));

		info("pcp: %s: internal port for %s is %u\n",
		     sdp_media_name(sdpm),
		     i==0 ? "RTP" : "RTCP",
		     map.int_port);

		err = pcp_request(&comp->pcp, NULL, &pcp_srv, PCP_MAP,
				  LIFETIME, &map, pcp_resp_handler, comp, 0);
		if (err)
			goto out;
	}

 out:
	if (err)
		mem_deref(m);
	else if (mp) {
		*mp = m;
	}

	return err;
}


static void media_refresh(struct mnat_media *media)
{
	unsigned i;

	for (i=0; i<media->compc; i++) {
		struct comp *comp = &media->compv[i];

		pcp_force_refresh(comp->pcp);
	}
}


static void refresh_session(struct mnat_sess *sess, uint32_t epoch_time)
{
	struct le *le;

	for (le = sess->medial.head; le; le = le->next) {

		struct mnat_media *m = le->data;

		if (epoch_time < m->srv_epoch) {
			info("pcp: detected PCP Server reboot!\n");
			media_refresh(m);
		}

		m->srv_epoch = epoch_time;
	}
}


static void pcp_msg_handler(const struct pcp_msg *msg, void *arg)
{
	struct le *le;

	(void)arg;

	info("pcp: received notification: %H\n", pcp_msg_print, msg);

	if (msg->hdr.opcode == PCP_ANNOUNCE) {

		for (le = sessl.head; le; le = le->next) {

			struct mnat_sess *sess = le->data;

			refresh_session(sess, msg->hdr.epoch);
		}
	}
}


static int module_init(void)
{
	struct pl pl;
	int err;

	if (0 == conf_get(conf_cur(), "pcp_server", &pl)) {
		err = sa_decode(&pcp_srv, pl.p, pl.l);
		if (err)
			return err;
	}
	else {
		err = net_default_gateway_get(net_af(baresip_network()),
					      &pcp_srv);
		if (err)
			return err;
		sa_set_port(&pcp_srv, PCP_PORT_SRV);
	}

	info("pcp: using PCP server at %J\n", &pcp_srv);

	/* NOTE: if multiple applications are listening on port 5350
	   then this will not work */
	err = pcp_listen(&lsnr, &pcp_srv, pcp_msg_handler, 0);
	if (err) {
		info("pcp: could not enable listener: %m\n", err);
		err = 0;
	}

	return mnat_register(&mnat, baresip_mnatl(), "pcp", NULL,
			     session_alloc, media_alloc, NULL);
}


static int module_close(void)
{
	lsnr = mem_deref(lsnr);
	mnat = mem_deref(mnat);
	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(pcp) = {
	"pcp",
	"mnat",
	module_init,
	module_close,
};
