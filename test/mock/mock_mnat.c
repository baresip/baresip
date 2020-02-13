/**
 * @file mock/mock_mnat.c Mock media NAT-traversal
 *
 * Copyright (C) 2010 - 2018 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "../test.h"


struct mnat_sess {
	struct list medial;
	struct tmr tmr;
	mnat_estab_h *estabh;
	void *arg;
};


struct mnat_media {
	struct le le;
	struct sdp_media *sdpm;
	mnat_connected_h *connh;
	void *arg;
};


static void sess_destructor(void *data)
{
	struct mnat_sess *sess = data;

	tmr_cancel(&sess->tmr);
	list_flush(&sess->medial);
}


static void media_destructor(void *arg)
{
	struct mnat_media *m = arg;

	list_unlink(&m->le);
	mem_deref(m->sdpm);
}


static void tmr_handler(void *data)
{
	struct mnat_sess *sess = data;

	if (sess->estabh)
		sess->estabh(0, 0, "ok", sess->arg);
}


static int mnat_session_alloc(struct mnat_sess **sessp,
			      const struct mnat *mnat, struct dnsc *dnsc,
			      int af, const struct stun_uri *srv,
			      const char *user, const char *pass,
			      struct sdp_session *sdp, bool offerer,
			      mnat_estab_h *estabh, void *arg)
{
	struct mnat_sess *sess;

	(void)dnsc;
	(void)af;
	(void)srv;
	(void)user;
	(void)pass;
	(void)sdp;
	(void)offerer;

	if (!sessp || !mnat)
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
			    struct sdp_media *sdpm,
			    mnat_connected_h *connh, void *arg)
{
	struct mnat_media *m;
	int err;

	(void)mp;
	(void)sess;
	(void)sock1;
	(void)sock2;
	(void)connh;
	(void)arg;

	if (!mp || !sess || !sock1 || !sdpm)
		return EINVAL;

	m = mem_zalloc(sizeof(*m), media_destructor);
	if (!m)
		return ENOMEM;

	err = sdp_media_set_lattr(sdpm, true, "xnat", NULL);
	if (err)
		goto out;

	m->sdpm  = mem_ref(sdpm);
	m->connh = connh;
	m->arg   = arg;

	list_append(&sess->medial, &m->le, m);

 out:
	if (err)
		mem_deref(m);
	else
		*mp = m;

	return err;
}


static int mnat_session_update(struct mnat_sess *sess)
{
	struct le *le;

	if (!sess)
		return EINVAL;

	for (le = sess->medial.head; le; le = le->next) {
		struct mnat_media *m = le->data;
		struct sa rtp, rtcp;

		rtp = *sdp_media_raddr(m->sdpm);
		sdp_media_raddr_rtcp(m->sdpm, &rtcp);

		if (sa_isset(&rtp, SA_ALL) &&
		    sa_isset(&rtcp, SA_ALL)) {

			if (m->connh)
				m->connh(&rtp, &rtcp, m->arg);
		}
	}

	return 0;
}


static struct mnat mnat_mock = {
	.id      = "XNAT",
	.wait_connected = true,
	.sessh   = mnat_session_alloc,
	.mediah  = mnat_media_alloc,
	.updateh = mnat_session_update,
};


void mock_mnat_register(struct list *mnatl)
{
	mnat_register(mnatl, &mnat_mock);
}


void mock_mnat_unregister(void)
{
	mnat_unregister(&mnat_mock);
}
