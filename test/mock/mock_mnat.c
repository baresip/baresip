/**
 * @file mock/mock_mnat.c Mock media NAT-traversal
 *
 * Copyright (C) 2010 - 2018 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "../test.h"


struct mnat_sess {
	struct tmr tmr;
	mnat_estab_h *estabh;
	void *arg;
};


static void sess_destructor(void *data)
{
	struct mnat_sess *sess = data;

	tmr_cancel(&sess->tmr);
}


static void tmr_handler(void *data)
{
	struct mnat_sess *sess = data;

	if (sess->estabh)
		sess->estabh(0, 0, "ok", sess->arg);
}


static int mnat_session_alloc(struct mnat_sess **sessp, struct dnsc *dnsc,
			      int af, const char *srv, uint16_t port,
			      const char *user, const char *pass,
			      struct sdp_session *sdp, bool offerer,
			      mnat_estab_h *estabh, void *arg)
{
	struct mnat_sess *sess;

	(void)dnsc;
	(void)af;
	(void)srv;
	(void)port;
	(void)user;
	(void)pass;
	(void)sdp;
	(void)offerer;

	if (!sessp)
		return EINVAL;

	sess = mem_zalloc(sizeof(*sess), sess_destructor);
	if (!sess)
		return ENOMEM;

	sess->estabh = estabh;
	sess->arg    = arg;

	/* Simulate async network traffic */
	tmr_start(&sess->tmr, 0, tmr_handler, sess);

	*sessp = sess;

	return 0;
}


static int mnat_media_alloc(struct mnat_media **mp, struct mnat_sess *sess,
			    struct udp_sock *sock1, struct udp_sock *sock2,
			    struct sdp_media *sdpm)
{
	int err;

	(void)mp;
	(void)sess;
	(void)sock1;
	(void)sock2;

	err = sdp_media_set_lattr(sdpm, true, "xnat", NULL);
	if (err)
		return err;

	return 0;
}


static struct mnat mnat_mock = {
	.id     = "XNAT",
	.sessh  = mnat_session_alloc,
	.mediah = mnat_media_alloc,
};


int mock_mnat_register(struct list *mnatl)
{
	mnat_register(mnatl, &mnat_mock);

	return 0;
}


void mock_mnat_unregister(void)
{
	mnat_unregister(&mnat_mock);
}
