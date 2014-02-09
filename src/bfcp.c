/**
 * @file bfcp.c  BFCP client
 *
 * Copyright (C) 2011 Creytiv.com
 */
#include <stdlib.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


struct bfcp {
	struct bfcp_conn *conn;
	struct sdp_media *sdpm;
	struct mnat_media *mnat_st;
	bool active;

	/* server */
	uint32_t lconfid;
	uint16_t luserid;
};


static void destructor(void *arg)
{
	struct bfcp *bfcp = arg;

	mem_deref(bfcp->mnat_st);
	mem_deref(bfcp->sdpm);
	mem_deref(bfcp->conn);
}


static const char *bfcp_sdp_transp(enum bfcp_transp tp)
{
	switch (tp) {

	case BFCP_UDP:  return "UDP/BFCP";
	case BFCP_DTLS: return "UDP/TLS/BFCP";
	default:        return NULL;
	}
}


static enum bfcp_transp str2tp(const char *proto)
{
	if (0 == str_casecmp(proto, "udp"))
		return BFCP_UDP;
	else if (0 == str_casecmp(proto, "dtls"))
		return BFCP_DTLS;
	else {
		warning("unsupported BFCP protocol: %s\n", proto);
		return -1;
	}
}


static void bfcp_resp_handler(int err, const struct bfcp_msg *msg, void *arg)
{
	struct bfcp *bfcp = arg;
	(void)bfcp;

	if (err) {
		warning("bfcp: error response: %m\n", err);
		return;
	}

	info("bfcp: received BFCP response: '%s'\n",
	     bfcp_prim_name(msg->prim));
}


static void bfcp_msg_handler(const struct bfcp_msg *msg, void *arg)
{
	struct bfcp *bfcp = arg;

	info("bfcp: received BFCP message '%s'\n", bfcp_prim_name(msg->prim));

	switch (msg->prim) {

	case BFCP_HELLO:
		(void)bfcp_reply(bfcp->conn, msg, BFCP_HELLO_ACK, 0);
		break;

	default:
		(void)bfcp_ereply(bfcp->conn, msg, BFCP_UNKNOWN_PRIM);
		break;
	}
}


int bfcp_alloc(struct bfcp **bfcpp, struct sdp_session *sdp_sess,
	       const char *proto, bool offerer,
	       const struct mnat *mnat, struct mnat_sess *mnat_sess)
{
	struct bfcp *bfcp;
	struct sa laddr;
	enum bfcp_transp transp;
	int err;

	if (!bfcpp || !sdp_sess)
		return EINVAL;

	transp = str2tp(proto);

	bfcp = mem_zalloc(sizeof(*bfcp), destructor);
	if (!bfcp)
		return ENOMEM;

	bfcp->active = offerer;

	sa_init(&laddr, AF_INET);

	err = bfcp_listen(&bfcp->conn, transp, &laddr, uag_tls(),
			  bfcp_msg_handler, bfcp);
	if (err)
		goto out;

	err = sdp_media_add(&bfcp->sdpm, sdp_sess, "application",
			    sa_port(&laddr), bfcp_sdp_transp(transp));
	if (err)
		goto out;

	err = sdp_format_add(NULL, bfcp->sdpm, false, "*", NULL,
			     0, 0, NULL, NULL, NULL, false, NULL);
	if (err)
		goto out;

	err |= sdp_media_set_lattr(bfcp->sdpm, true, "floorctrl", "c-s");
	err |= sdp_media_set_lattr(bfcp->sdpm, true, "setup",
				   bfcp->active ? "active" : "actpass");

	if (bfcp->active) {
		err |= sdp_media_set_lattr(bfcp->sdpm, true,
					   "connection", "new");
	}
	else {
		bfcp->lconfid = 1000 + (rand_u16() & 0xf);
		bfcp->luserid = 1    + (rand_u16() & 0x7);

		err |= sdp_media_set_lattr(bfcp->sdpm, true, "confid",
					   "%u", bfcp->lconfid);
		err |= sdp_media_set_lattr(bfcp->sdpm, true, "userid",
					   "%u", bfcp->luserid);
	}

	if (err)
		goto out;

	if (mnat) {
		info("bfcp: enabled medianat '%s' on UDP socket\n", mnat->id);

		err = mnat->mediah(&bfcp->mnat_st, mnat_sess, IPPROTO_UDP,
				   bfcp_sock(bfcp->conn), NULL, bfcp->sdpm);
		if (err)
			goto out;
	}

	info("bfcp: %s BFCP agent protocol '%s' on port %d\n",
	     bfcp->active ? "Active" : "Passive",
	     proto, sa_port(&laddr));

 out:
	if (err)
		mem_deref(bfcp);
	else
		*bfcpp = bfcp;

	return err;
}


int bfcp_start(struct bfcp *bfcp)
{
	const struct sa *paddr;
	uint32_t confid = 0;
	uint16_t userid = 0;
	int err = 0;

	if (!bfcp)
		return EINVAL;

	if (!sdp_media_rport(bfcp->sdpm)) {
		info("bfcp channel is disabled\n");
		return 0;
	}

	if (bfcp->active) {

		paddr  = sdp_media_raddr(bfcp->sdpm);
		confid = sdp_media_rattr_u32(bfcp->sdpm, "confid");
		userid = sdp_media_rattr_u32(bfcp->sdpm, "userid");

		err = bfcp_request(bfcp->conn, paddr, BFCP_VER2, BFCP_HELLO,
				   confid, userid, bfcp_resp_handler, bfcp, 0);
	}

	return err;
}
