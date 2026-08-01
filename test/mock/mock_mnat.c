/**
 * @file mock/mock_mnat.c Mock media NAT-traversal
 *
 * Copyright (C) 2010 - 2018 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "../test.h"


void mock_mnat_media_gather_defer(bool defer);
void mock_mnat_media_gather_result(int err);
void mock_mnat_complete_media_gathers(void);
unsigned mock_mnat_media_gather_cancel_count(void);
unsigned mock_mnat_media_gather_callback_count(void);


struct mnat_sess {
	struct list medial;
	struct tmr tmr;
	uint64_t next_generation;
	mnat_estab_h *estabh;
	void *arg;
};


struct mnat_media {
	struct le le;
	struct le attempt_le;
	struct le gather_le;
	struct sdp_media *sdpm;
	struct mnat_sess *sess;
	uint64_t generation;
	struct tmr tmr;
	struct tmr attempt_tmr;
	mnat_connected_h *connh;
	mnat_media_attempt_h *attempth;
	mnat_media_gather_h *gatherh;
	void *arg;
	void *attempt_arg;
	void *gather_arg;
	struct sa trickle_remote;
	int gather_err;
	bool active;
	bool terminated;
	bool gathered;
	bool prepared;
	bool activated;
	bool attempt_running;
	bool gather_waiting;
	bool prepared_active;
	bool rollback_active;
};

static int media_prepare_error;
static int media_attempt_error;
static bool media_attempt_deferred;
static unsigned media_attempt_starts;
static unsigned media_attempt_cancels;
static unsigned media_attempt_callbacks;
static struct list media_attemptl;
static int media_gather_error;
static bool media_gather_deferred;
static unsigned media_gather_cancels;
static unsigned media_gather_callbacks;
static struct list media_gatherl;
static unsigned candidate_attr_count;
static unsigned candidate_attr_counts[32];
static uint64_t last_candidate_generation;

static void media_attempt_cancel(struct mnat_media *m);
static void media_gather_cancel(struct mnat_media *m);


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
	list_unlink(&m->attempt_le);
	list_unlink(&m->gather_le);
	tmr_cancel(&m->tmr);
	tmr_cancel(&m->attempt_tmr);
	m->attempt_running = false;
	m->attempth = NULL;
	m->attempt_arg = NULL;
	m->gather_waiting = false;
	m->gatherh = NULL;
	m->gather_arg = NULL;
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
	err |= sdp_media_set_lattr(sdpm, true, "ice-ufrag", "mock-%llu",
				   (unsigned long long)m->generation);
	err |= sdp_media_set_lattr(sdpm, true, "ice-pwd", "mock-password-%llu",
				   (unsigned long long)m->generation);
	if (err)
		goto out;

	m->sdpm  = mem_ref(sdpm);
	m->sess  = sess;
	m->generation = ++sess->next_generation;
	m->connh = connh;
	m->arg   = arg;
	m->active = true;
	m->gathered = !media_gather_deferred;

	list_append(&sess->medial, &m->le, m);

 out:
	if (err)
		mem_deref(m);
	else
		*mp = m;

	return err;
}


static int mock_media_restart_alloc(struct mnat_media **candidatep,
				    struct mnat_media *active,
				    struct udp_sock *sock,
				    struct sdp_media *sdpm,
				    mnat_connected_h *connh, void *arg)
{
	struct mnat_media *m;
	int err;

	if (!candidatep || !active || !active->sess || !active->active ||
	    !sock || !sdpm)
		return EINVAL;

	m = mem_zalloc(sizeof(*m), media_destructor);
	if (!m)
		return ENOMEM;

	m->sess = active->sess;
	m->sdpm = mem_ref(sdpm);
	m->connh = connh;
	m->arg = arg;
	m->generation = ++m->sess->next_generation;
	m->gathered = !media_gather_deferred;

	err = sdp_media_set_lattr(sdpm, true, "xnat", NULL);
	err |= sdp_media_set_lattr(sdpm, true, "ice-ufrag", "mock-%llu",
				   (unsigned long long)m->generation);
	err |= sdp_media_set_lattr(sdpm, true, "ice-pwd", "mock-password-%llu",
				   (unsigned long long)m->generation);
	if (err)
		goto out;

	list_append(&m->sess->medial, &m->le, m);

out:
	if (err)
		mem_deref(m);
	else
		*candidatep = m;
	return err;
}


static void update_handler(void *data)
{
	struct mnat_media *m = data;
	struct sa rtp, rtcp;

	rtp = *sdp_media_raddr(m->sdpm);
	sdp_media_raddr_rtcp(m->sdpm, &rtcp);
	if (!m->active || m->prepared)
		return;

	if (sa_isset(&rtp, SA_ALL) && sa_isset(&rtcp, SA_ALL)) {

		if (m->connh)
			m->connh(&rtp, &rtcp, m->arg);
	}
}


static void attr_handler(struct mnat_media *m, const char *name,
			 const char *value)
{
	struct ice_cand_attr candidate;
	const char *encoded = value;

	if (!m || str_cmp(name, "candidate"))
		return;
	++candidate_attr_count;
	last_candidate_generation = m->generation;
	if (m->generation < RE_ARRAY_SIZE(candidate_attr_counts))
		++candidate_attr_counts[m->generation];
	if (!str_isset(encoded))
		return;
	if (!strncmp(encoded, "candidate:", 10))
		encoded += 10;
	if (!ice_cand_attr_decode(&candidate, encoded) && candidate.compid == 1)
		sa_cpy(&m->trickle_remote, &candidate.addr);
}


static int mnat_session_update(struct mnat_sess *sess)
{
	struct le *le;

	if (!sess)
		return EINVAL;

	for (le = sess->medial.head; le; le = le->next) {
		struct mnat_media *m = le->data;

		if (m->active && !m->prepared)
			tmr_start(&m->tmr, 0, update_handler, m);
	}

	return 0;
}


static int media_prepare(struct mnat_media *m, bool active)
{
	if (!m)
		return EINVAL;
	if (m->terminated)
		return ECANCELED;
	if (media_prepare_error) {
		int err = media_prepare_error;

		media_prepare_error = 0;
		return err;
	}
	if (m->prepared || m->activated)
		return EALREADY;
	m->prepared_active = active;
	m->prepared = true;
	return 0;
}


static void media_activate(struct mnat_media *m)
{
	if (!m || !m->prepared || m->activated)
		return;
	m->rollback_active = m->active;
	m->active = m->prepared_active;
	m->prepared = false;
	m->activated = true;
}


static void media_rollback(struct mnat_media *m)
{
	if (!m || !m->activated)
		return;
	m->active = m->rollback_active;
	m->activated = false;
}


static void media_finalize(struct mnat_media *m)
{
	if (m)
		m->activated = false;
}


static void media_abort(struct mnat_media *m)
{
	if (m && m->prepared && !m->activated) {
		if (!m->active)
			m->terminated = true;
		media_gather_cancel(m);
		media_attempt_cancel(m);
		m->prepared = false;
	}
}


static void media_attempt_complete(struct mnat_media *m)
{
	mnat_media_attempt_h *attempth;
	struct sa rtp = {0};
	struct sa rtcp = {0};
	void *arg;
	int err;

	if (!m || !m->attempt_running)
		return;

	m = mem_ref(m);
	attempth = m->attempth;
	arg = m->attempt_arg;
	err = media_attempt_error;
	if (!err) {
		rtp = sa_isset(&m->trickle_remote, SA_ALL)
			    ? m->trickle_remote : *sdp_media_raddr(m->sdpm);
		sdp_media_raddr_rtcp(m->sdpm, &rtcp);
		if (!sa_isset(&rtp, SA_ALL))
			err = EDESTADDRREQ;
	}

	tmr_cancel(&m->attempt_tmr);
	list_unlink(&m->attempt_le);
	m->attempt_running = false;
	m->attempth = NULL;
	m->attempt_arg = NULL;
	++media_attempt_callbacks;

	if (attempth)
		attempth(err, err ? NULL : &rtp,
			 sa_isset(&rtcp, SA_ALL) ? &rtcp : NULL, arg);

	mem_deref(m);
}


static void media_attempt_tmr_handler(void *arg)
{
	media_attempt_complete(arg);
}


static int media_attempt_start(struct mnat_media *m,
			       mnat_media_attempt_h *attempth, void *arg)
{
	if (!m || !attempth)
		return EINVAL;
	if (m->attempt_running)
		return EALREADY;
	if (!m->prepared || !m->prepared_active)
		return EINVAL;

	m->attempth = attempth;
	m->attempt_arg = arg;
	m->attempt_running = true;
	list_append(&media_attemptl, &m->attempt_le, m);
	++media_attempt_starts;
	if (!media_attempt_deferred)
		tmr_start(&m->attempt_tmr, 0, media_attempt_tmr_handler, m);

	return 0;
}


static void media_attempt_cancel(struct mnat_media *m)
{
	if (!m || !m->attempt_running)
		return;

	tmr_cancel(&m->attempt_tmr);
	list_unlink(&m->attempt_le);
	m->attempt_running = false;
	m->attempth = NULL;
	m->attempt_arg = NULL;
	++media_attempt_cancels;
}


static bool media_gathered(const struct mnat_media *m)
{
	return m && m->gathered;
}


static int media_gather_wait(struct mnat_media *m,
			     mnat_media_gather_h *gatherh, void *arg)
{
	if (!m || !gatherh)
		return EINVAL;
	if (!m->prepared || !m->prepared_active)
		return EINVAL;
	if (m->gather_err)
		return m->gather_err;
	if (m->gathered)
		return 0;
	if (m->gather_waiting)
		return EALREADY;

	m->gatherh = gatherh;
	m->gather_arg = arg;
	m->gather_waiting = true;
	list_append(&media_gatherl, &m->gather_le, m);
	return EAGAIN;
}


static void media_gather_cancel(struct mnat_media *m)
{
	if (!m || !m->gather_waiting)
		return;

	list_unlink(&m->gather_le);
	m->gather_waiting = false;
	m->gatherh = NULL;
	m->gather_arg = NULL;
	++media_gather_cancels;
}


static void media_gather_complete(struct mnat_media *m)
{
	mnat_media_gather_h *gatherh;
	void *arg;
	int err;

	if (!m || !m->gather_waiting)
		return;

	m = mem_ref(m);
	gatherh = m->gatherh;
	arg = m->gather_arg;
	err = media_gather_error;
	list_unlink(&m->gather_le);
	m->gather_waiting = false;
	m->gatherh = NULL;
	m->gather_arg = NULL;
	m->gather_err = err;
	m->gathered = !err;
	++media_gather_callbacks;
	if (gatherh)
		gatherh(err, arg);
	mem_deref(m);
}


static struct mnat mnat_mock = {
	.id      = "XNAT",
	.wait_connected = true,
	.sessh   = mnat_session_alloc,
	.mediah  = mnat_media_alloc,
	.mediarestartalloch = mock_media_restart_alloc,
	.updateh = mnat_session_update,
	.attrh = attr_handler,
	.mediaprepareh = media_prepare,
	.mediaactivateh = media_activate,
	.mediarollbackh = media_rollback,
	.mediafinalizeh = media_finalize,
	.mediaaborth = media_abort,
	.mediaattemptstarth = media_attempt_start,
	.mediaattemptcancelh = media_attempt_cancel,
	.mediagatheredh = media_gathered,
	.mediagatherwaith = media_gather_wait,
	.mediagathercancelh = media_gather_cancel,
};


void mock_mnat_register(struct list *mnatl)
{
	mnat_register(mnatl, &mnat_mock);
}


void mock_mnat_unregister(void)
{
	struct le *le;

	while ((le = list_head(&media_attemptl)))
		media_attempt_cancel(le->data);
	while ((le = list_head(&media_gatherl)))
		media_gather_cancel(le->data);
	media_prepare_error = 0;
	media_attempt_error = 0;
	media_attempt_deferred = false;
	media_attempt_starts = 0;
	media_attempt_cancels = 0;
	media_attempt_callbacks = 0;
	media_gather_error = 0;
	media_gather_deferred = false;
	candidate_attr_count = 0;
	memset(candidate_attr_counts, 0, sizeof(candidate_attr_counts));
	last_candidate_generation = 0;
	media_gather_cancels = 0;
	media_gather_callbacks = 0;
	mnat_unregister(&mnat_mock);
}


void mock_mnat_fail_media_prepare(int err)
{
	media_prepare_error = err;
}


void mock_mnat_media_attempt_result(int err)
{
	media_attempt_error = err;
}


void mock_mnat_media_attempt_defer(bool defer)
{
	media_attempt_deferred = defer;
}


void mock_mnat_complete_media_attempts(void)
{
	struct le *le;

	while ((le = list_head(&media_attemptl)))
		media_attempt_complete(le->data);
}


unsigned mock_mnat_media_attempt_start_count(void)
{
	return media_attempt_starts;
}


unsigned mock_mnat_media_attempt_cancel_count(void)
{
	return media_attempt_cancels;
}


unsigned mock_mnat_media_attempt_callback_count(void)
{
	return media_attempt_callbacks;
}


void mock_mnat_media_gather_defer(bool defer)
{
	media_gather_deferred = defer;
}


void mock_mnat_media_gather_result(int err)
{
	media_gather_error = err;
}


void mock_mnat_complete_media_gathers(void)
{
	struct le *le;

	while ((le = list_head(&media_gatherl)))
		media_gather_complete(le->data);
}


unsigned mock_mnat_media_gather_cancel_count(void)
{
	return media_gather_cancels;
}


unsigned mock_mnat_media_gather_callback_count(void)
{
	return media_gather_callbacks;
}


uint64_t mock_mnat_media_generation(const struct mnat_media *m)
{
	return m ? m->generation : 0;
}


bool mock_mnat_media_is_active(const struct mnat_media *m)
{
	return m && m->active;
}


unsigned mock_mnat_candidate_attr_count(void)
{
	return candidate_attr_count;
}


unsigned mock_mnat_candidate_attr_count_generation(uint64_t generation)
{
	return generation < RE_ARRAY_SIZE(candidate_attr_counts)
		? candidate_attr_counts[generation] : 0;
}


uint64_t mock_mnat_last_candidate_generation(void)
{
	return last_candidate_generation;
}
