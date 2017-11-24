/**
 * @file ice.c ICE Module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCNetworkReachability.h>
#endif
#include <re.h>
#include <baresip.h>


/**
 * @defgroup ice ice
 *
 * Interactive Connectivity Establishment (ICE) for media NAT traversal
 *
 * This module enables ICE for NAT traversal. You can enable ICE
 * in your accounts file with the parameter ;medianat=ice. The following
 * options can be configured:
 *
 \verbatim
  ice_turn        {yes,no}             # Enable TURN candidates
  ice_debug       {yes,no}             # Enable ICE debugging/tracing
  ice_nomination  {regular,aggressive} # Regular or aggressive nomination
  ice_mode        {full,lite}          # Full ICE-mode or ICE-lite
 \endverbatim
 */


enum {
	ICE_LAYER = 0
};


struct mnat_sess {
	struct list medial;
	struct sa srv;
	struct stun_dns *dnsq;
	struct sdp_session *sdp;
	char lufrag[8];
	char lpwd[32];
	uint64_t tiebrk;
	bool offerer;
	char *user;
	char *pass;
	int mediac;
	bool started;
	bool send_reinvite;
	mnat_estab_h *estabh;
	void *arg;
};

struct mnat_media {
	struct comp {
		struct mnat_media *m;         /* pointer to parent */
		struct stun_ctrans *ct_gath;
		struct sa laddr;
		unsigned id;
		void *sock;
	} compv[2];
	struct le le;
	struct mnat_sess *sess;
	struct sdp_media *sdpm;
	struct icem *icem;
	bool complete;
	bool terminated;
	int nstun;                   /**< Number of pending STUN candidates  */
};


static struct mnat *mnat;
static struct {
	enum ice_mode mode;
	enum ice_nomination nom;
	bool turn;
	bool debug;
} ice = {
	ICE_MODE_FULL,
	ICE_NOMINATION_REGULAR,
	true,
	false
};


static void gather_handler(int err, uint16_t scode, const char *reason,
			   void *arg);


static void call_gather_handler(int err, struct mnat_media *m, uint16_t scode,
				const char *reason)
{

	/* No more pending requests? */
	if (m->nstun != 0)
		return;

	debug("ice: all components gathered.\n");

	if (err)
		goto out;

	/* Eliminate redundant local candidates */
	icem_cand_redund_elim(m->icem);

	err = icem_comps_set_default_cand(m->icem);
	if (err) {
		warning("ice: set default cands failed (%m)\n", err);
		goto out;
	}

 out:
	gather_handler(err, scode, reason, m);
}


static void stun_resp_handler(int err, uint16_t scode, const char *reason,
			      const struct stun_msg *msg, void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->m;
	struct stun_attr *attr;
	struct ice_cand *lcand;

	if (m->terminated)
		return;

	--m->nstun;

	if (err || scode > 0) {
		warning("ice: comp %u: STUN Request failed: %m\n",
			comp->id, err);
		goto out;
	}

	debug("ice: srflx gathering for comp %u complete.\n", comp->id);

	/* base candidate */
	lcand = icem_cand_find(icem_lcandl(m->icem), comp->id, NULL);
	if (!lcand)
		goto out;

	attr = stun_msg_attr(msg, STUN_ATTR_XOR_MAPPED_ADDR);
	if (!attr)
		attr = stun_msg_attr(msg, STUN_ATTR_MAPPED_ADDR);
	if (!attr) {
		warning("ice: no Mapped Address in Response\n");
		err = EPROTO;
		goto out;
	}

	err = icem_lcand_add(m->icem, icem_lcand_base(lcand),
			     ICE_CAND_TYPE_SRFLX,
			     &attr->v.sa);

 out:
	call_gather_handler(err, m, scode, reason);
}


/** Gather Server Reflexive address */
static int send_binding_request(struct mnat_media *m, struct comp *comp)
{
	int err;

	if (comp->ct_gath)
		return EALREADY;

	debug("ice: gathering srflx for comp %u ..\n", comp->id);

	err = stun_request(&comp->ct_gath, icem_stun(m->icem), IPPROTO_UDP,
			   comp->sock, &m->sess->srv, 0,
			   STUN_METHOD_BINDING,
			   NULL, false, 0,
			   stun_resp_handler, comp, 1,
			   STUN_ATTR_SOFTWARE, stun_software);
	if (err)
		return err;

	++m->nstun;

	return 0;
}


static void turnc_handler(int err, uint16_t scode, const char *reason,
			  const struct sa *relay, const struct sa *mapped,
			  const struct stun_msg *msg, void *arg)
{
	struct comp *comp = arg;
	struct mnat_media *m = comp->m;
	struct ice_cand *lcand;
	(void)msg;

	--m->nstun;

	/* TURN failed, so we destroy the client */
	if (err || scode) {
		icem_set_turn_client(m->icem, comp->id, NULL);
	}

	if (err) {
		warning("{%u} TURN Client error: %m\n",
			      comp->id, err);
		goto out;
	}

	if (scode) {
		warning("{%u} TURN Client error: %u %s\n",
			      comp->id, scode, reason);
		err = send_binding_request(m, comp);
		if (err)
			goto out;
		return;
	}

	debug("ice: relay gathered for comp %u (%u %s)\n",
	      comp->id, scode, reason);

	lcand = icem_cand_find(icem_lcandl(m->icem), comp->id, NULL);
	if (!lcand)
		goto out;

	if (!sa_cmp(relay, icem_lcand_addr(icem_lcand_base(lcand)), SA_ALL)) {
		err = icem_lcand_add(m->icem, icem_lcand_base(lcand),
				     ICE_CAND_TYPE_RELAY, relay);
	}

	if (mapped) {
		err |= icem_lcand_add(m->icem, icem_lcand_base(lcand),
				      ICE_CAND_TYPE_SRFLX, mapped);
	}
	else {
		err |= send_binding_request(m, comp);
	}

 out:
	call_gather_handler(err, m, scode, reason);
}


static int cand_gather_relayed(struct mnat_media *m, struct comp *comp,
			       const char *username, const char *password)
{
	struct turnc *turnc = NULL;
	const int layer = ICE_LAYER - 10; /* below ICE stack */
	int err;

	err = turnc_alloc(&turnc, stun_conf(icem_stun(m->icem)),
			  IPPROTO_UDP, comp->sock, layer, &m->sess->srv,
			  username, password,
			  60, turnc_handler, comp);
	if (err)
		return err;

	err = icem_set_turn_client(m->icem, comp->id, turnc);
	if (err)
		goto out;

	++m->nstun;

 out:
	mem_deref(turnc);

	return err;
}


static int start_gathering(struct mnat_media *m,
			   const char *username, const char *password)
{
	unsigned i;
	int err = 0;

	if (ice.mode != ICE_MODE_FULL)
		return EINVAL;

	/* for each component */
	for (i=0; i<2; i++) {
		struct comp *comp = &m->compv[i];

		if (!comp->sock)
			continue;

		if (username && password) {
			err |= cand_gather_relayed(m, comp,
						   username, password);
		}
		else
			err |= send_binding_request(m, comp);
	}

	return err;
}


static int icem_gather_srflx(struct mnat_media *m)
{
	if (!m)
		return EINVAL;

	return start_gathering(m, NULL, NULL);
}


static int icem_gather_relay(struct mnat_media *m,
		      const char *username, const char *password)
{
	if (!m || !username || !password)
		return EINVAL;

	return start_gathering(m, username, password);
}


static bool is_cellular(const struct sa *laddr)
{
#if TARGET_OS_IPHONE
	SCNetworkReachabilityRef r;
	SCNetworkReachabilityFlags flags = 0;
	bool cell = false;

	r = SCNetworkReachabilityCreateWithAddressPair(NULL,
						       &laddr->u.sa, NULL);
	if (!r)
		return false;

	if (SCNetworkReachabilityGetFlags(r, &flags)) {

		if (flags & kSCNetworkReachabilityFlagsIsWWAN)
			cell = true;
	}

	CFRelease(r);

	return cell;
#else
	(void)laddr;
	return false;
#endif
}


static void ice_printf(struct mnat_media *m, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	debug("%s: %v", m ? sdp_media_name(m->sdpm) : "ICE", fmt, &ap);
	va_end(ap);
}


static void session_destructor(void *arg)
{
	struct mnat_sess *sess = arg;

	list_flush(&sess->medial);
	mem_deref(sess->dnsq);
	mem_deref(sess->user);
	mem_deref(sess->pass);
	mem_deref(sess->sdp);
}


static void media_destructor(void *arg)
{
	struct mnat_media *m = arg;
	unsigned i;

	m->terminated = true;

	list_unlink(&m->le);
	mem_deref(m->sdpm);
	mem_deref(m->icem);
	for (i=0; i<2; i++) {
		mem_deref(m->compv[i].ct_gath);
		mem_deref(m->compv[i].sock);
	}
}


static bool candidate_handler(struct le *le, void *arg)
{
	return 0 != sdp_media_set_lattr(arg, false, ice_attr_cand, "%H",
					ice_cand_encode, le->data);
}


/**
 * Update the local SDP attributes, this can be called multiple times
 * when the state of the ICE machinery changes
 */
static int set_media_attributes(struct mnat_media *m)
{
	int err = 0;

	if (icem_mismatch(m->icem)) {
		err = sdp_media_set_lattr(m->sdpm, true,
					  ice_attr_mismatch, NULL);
		return err;
	}
	else {
		sdp_media_del_lattr(m->sdpm, ice_attr_mismatch);
	}

	/* Encode all my candidates */
	sdp_media_del_lattr(m->sdpm, ice_attr_cand);
	if (list_apply(icem_lcandl(m->icem), true, candidate_handler, m->sdpm))
		return ENOMEM;

	if (ice_remotecands_avail(m->icem)) {
		err |= sdp_media_set_lattr(m->sdpm, true,
					   ice_attr_remote_cand, "%H",
					   ice_remotecands_encode, m->icem);
	}

	return err;
}


static bool if_handler(const char *ifname, const struct sa *sa, void *arg)
{
	struct mnat_media *m = arg;
	uint16_t lprio;
	unsigned i;
	int err = 0;

	/* Skip loopback and link-local addresses */
	if (sa_is_loopback(sa) || sa_is_linklocal(sa))
		return false;

	lprio = is_cellular(sa) ? 0 : 10;

	ice_printf(m, "added interface: %s:%j (local prio %u)\n",
		   ifname, sa, lprio);

	for (i=0; i<2; i++) {
		if (m->compv[i].sock)
			err |= icem_cand_add(m->icem, i+1, lprio, ifname, sa);
	}

	if (err) {
		warning("ice: %s:%j: icem_cand_add: %m\n", ifname, sa, err);
	}

	return false;
}


static int media_start(struct mnat_sess *sess, struct mnat_media *m)
{
	int err = 0;

	net_if_apply(if_handler, m);

	switch (ice.mode) {

	default:
	case ICE_MODE_FULL:
		if (ice.turn) {
			err = icem_gather_relay(m,
						sess->user, sess->pass);
		}
		else {
			err = icem_gather_srflx(m);
		}
		break;

	case ICE_MODE_LITE:
		err = icem_lite_set_default_candidates(m->icem);
		if (err) {
			warning("ice: could not set"
				" default candidates (%m)\n", err);
			return err;
		}

		gather_handler(0, 0, NULL, m);
		break;
	}

	return err;
}


static void dns_handler(int err, const struct sa *srv, void *arg)
{
	struct mnat_sess *sess = arg;
	struct le *le;

	if (err)
		goto out;

	debug("ice: resolved %s-server to address %J\n",
	      ice.turn ? "TURN" : "STUN", srv);

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


static int session_alloc(struct mnat_sess **sessp, struct dnsc *dnsc,
			 int af, const char *srv, uint16_t port,
			 const char *user, const char *pass,
			 struct sdp_session *ss, bool offerer,
			 mnat_estab_h *estabh, void *arg)
{
	struct mnat_sess *sess;
	const char *usage;
	int err;

	if (!sessp || !dnsc || !srv || !user || !pass || !ss || !estabh)
		return EINVAL;

	info("ice: new session with %s-server at %s (username=%s)\n",
	     ice.turn ? "TURN" : "STUN",
	     srv, user);

	sess = mem_zalloc(sizeof(*sess), session_destructor);
	if (!sess)
		return ENOMEM;

	sess->sdp    = mem_ref(ss);
	sess->estabh = estabh;
	sess->arg    = arg;

	err  = str_dup(&sess->user, user);
	err |= str_dup(&sess->pass, pass);
	if (err)
		goto out;

	rand_str(sess->lufrag, sizeof(sess->lufrag));
	rand_str(sess->lpwd,   sizeof(sess->lpwd));
	sess->tiebrk = rand_u64();
	sess->offerer = offerer;

	if (ICE_MODE_LITE == ice.mode) {
		err |= sdp_session_set_lattr(ss, true,
					     ice_attr_lite, NULL);
	}

	err |= sdp_session_set_lattr(ss, true,
				     ice_attr_ufrag, sess->lufrag);
	err |= sdp_session_set_lattr(ss, true,
				     ice_attr_pwd, sess->lpwd);
	if (err)
		goto out;

	usage = ice.turn ? stun_usage_relay : stun_usage_binding;

	err = stun_server_discover(&sess->dnsq, dnsc, usage, stun_proto_udp,
				   af, srv, port, dns_handler, sess);

 out:
	if (err)
		mem_deref(sess);
	else
		*sessp = sess;

	return err;
}


static bool verify_peer_ice(struct mnat_sess *ms)
{
	struct le *le;

	for (le = ms->medial.head; le; le = le->next) {
		struct mnat_media *m = le->data;
		struct sa raddr[2];
		unsigned i;

		if (!sdp_media_has_media(m->sdpm)) {
			info("ice: stream '%s' is disabled -- ignore\n",
			     sdp_media_name(m->sdpm));
			continue;
		}

		raddr[0] = *sdp_media_raddr(m->sdpm);
		sdp_media_raddr_rtcp(m->sdpm, &raddr[1]);

		for (i=0; i<2; i++) {
			if (m->compv[i].sock &&
			    !icem_verify_support(m->icem, i+1, &raddr[i])) {
				warning("ice: %s.%u: no remote candidates"
					" found (address = %J)\n",
					sdp_media_name(m->sdpm),
					i+1, &raddr[i]);
				return false;
			}
		}
	}

	return true;
}


static bool refresh_comp_laddr(struct mnat_media *m, unsigned id,
			       struct comp *comp, const struct sa *laddr)
{
	bool changed = false;

	if (!m || !comp || !comp->sock || !laddr)
		return false;

	if (!sa_cmp(&comp->laddr, laddr, SA_ALL)) {
		changed = true;

		ice_printf(m, "comp%u setting local: %J\n", id, laddr);
	}

	sa_cpy(&comp->laddr, laddr);

	if (id == 1)
		sdp_media_set_laddr(m->sdpm, &comp->laddr);
	else if (id == 2)
		sdp_media_set_laddr_rtcp(m->sdpm, &comp->laddr);

	return changed;
}


/*
 * Update SDP Media with local addresses
 */
static bool refresh_laddr(struct mnat_media *m,
			  const struct sa *laddr1,
			  const struct sa *laddr2)
{
	bool changed = false;

	changed |= refresh_comp_laddr(m, 1, &m->compv[0], laddr1);
	changed |= refresh_comp_laddr(m, 2, &m->compv[1], laddr2);

	return changed;
}


static void gather_handler(int err, uint16_t scode, const char *reason,
			   void *arg)
{
	struct mnat_media *m = arg;
	mnat_estab_h *estabh = m->sess->estabh;

	if (err || scode) {
		warning("ice: gather error: %m (%u %s)\n",
			err, scode, reason);
	}
	else {
		refresh_laddr(m,
			      icem_cand_default(m->icem, 1),
			      icem_cand_default(m->icem, 2));

		info("ice: %s: Default local candidates: %J / %J\n",
		     sdp_media_name(m->sdpm),
		     &m->compv[0].laddr, &m->compv[1].laddr);

		(void)set_media_attributes(m);

		if (--m->sess->mediac)
			return;
	}

	if (err || scode)
		m->sess->estabh = NULL;

	if (estabh)
		estabh(err, scode, reason, m->sess->arg);
}


static void conncheck_handler(int err, bool update, void *arg)
{
	struct mnat_media *m = arg;
	struct mnat_sess *sess = m->sess;
	struct le *le;

	info("ice: %s: connectivity check is complete (update=%d)\n",
	     sdp_media_name(m->sdpm), update);

	ice_printf(m, "Dumping media state: %H\n", icem_debug, m->icem);

	if (err) {
		warning("ice: connectivity check failed: %m\n", err);
	}
	else {
		bool changed;

		m->complete = true;

		changed = refresh_laddr(m,
					icem_selected_laddr(m->icem, 1),
					icem_selected_laddr(m->icem, 2));
		if (changed)
			sess->send_reinvite = true;

		(void)set_media_attributes(m);

		/* Check all conncheck flags */
		LIST_FOREACH(&sess->medial, le) {
			struct mnat_media *mx = le->data;
			if (!mx->complete)
				return;
		}
	}

	/* call estab-handler and send re-invite */
	if (sess->send_reinvite && update) {

		info("ice: %s: sending Re-INVITE with updated"
		     " default candidates\n",
		     sdp_media_name(m->sdpm));

		sess->estabh(0, 0, NULL, sess->arg);
		sess->send_reinvite = false;
	}
}


static int ice_start(struct mnat_sess *sess)
{
	struct le *le;
	int err = 0;

	/* Update SDP media */
	if (sess->started) {

		LIST_FOREACH(&sess->medial, le) {
			struct mnat_media *m = le->data;

			ice_printf(NULL, "ICE Start: %H",
				   icem_debug, m->icem);

			icem_update(m->icem);

			refresh_laddr(m,
				      icem_selected_laddr(m->icem, 1),
				      icem_selected_laddr(m->icem, 2));

			err |= set_media_attributes(m);
		}

		return err;
	}

	/* Clear all conncheck flags */
	LIST_FOREACH(&sess->medial, le) {
		struct mnat_media *m = le->data;

		if (sdp_media_has_media(m->sdpm)) {
			m->complete = false;

			if (ice.mode == ICE_MODE_FULL) {
				err = icem_conncheck_start(m->icem);
				if (err)
					return err;

				/* set the pair states
				   -- first media stream only */
				if (sess->medial.head == le) {
					ice_candpair_set_states(m->icem);
				}
			}
		}
		else {
			m->complete = true;
		}
	}

	sess->started = true;

	return 0;
}


static int media_alloc(struct mnat_media **mp, struct mnat_sess *sess,
		       int proto, void *sock1, void *sock2,
		       struct sdp_media *sdpm)
{
	struct mnat_media *m;
	enum ice_role role;
	unsigned i;
	int err = 0;

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

	if (sess->offerer)
		role = ICE_ROLE_CONTROLLING;
	else
		role = ICE_ROLE_CONTROLLED;

	err = icem_alloc(&m->icem, ice.mode, role,
			 proto, ICE_LAYER,
			 sess->tiebrk, sess->lufrag, sess->lpwd,
			 conncheck_handler, m);
	if (err)
		goto out;

	icem_conf(m->icem)->nom   = ice.nom;
	icem_conf(m->icem)->debug = ice.debug;
	icem_conf(m->icem)->rc    = 4;

	icem_set_conf(m->icem, icem_conf(m->icem));

	icem_set_name(m->icem, sdp_media_name(sdpm));

	for (i=0; i<2; i++) {
		m->compv[i].m = m;
		m->compv[i].id = i+1;
		if (m->compv[i].sock)
			err |= icem_comp_add(m->icem, i+1, m->compv[i].sock);
	}

	if (sa_isset(&sess->srv, SA_ALL))
		err |= media_start(sess, m);

 out:
	if (err)
		mem_deref(m);
	else {
		*mp = m;
		++sess->mediac;
	}

	return err;
}


static bool sdp_attr_handler(const char *name, const char *value, void *arg)
{
	struct mnat_sess *sess = arg;
	struct le *le;

	for (le = sess->medial.head; le; le = le->next) {
		struct mnat_media *m = le->data;

		(void)ice_sdp_decode(m->icem, name, value);
	}

	return false;
}


static bool media_attr_handler(const char *name, const char *value, void *arg)
{
	struct mnat_media *m = arg;
	return 0 != icem_sdp_decode(m->icem, name, value);
}


static int enable_turn_channels(struct mnat_sess *sess)
{
	struct le *le;
	int err = 0;

	for (le = sess->medial.head; le; le = le->next) {

		struct mnat_media *m = le->data;
		struct sa raddr[2];
		unsigned i;

		err |= set_media_attributes(m);

		raddr[0] = *sdp_media_raddr(m->sdpm);
		sdp_media_raddr_rtcp(m->sdpm, &raddr[1]);

		for (i=0; i<2; i++) {
			if (m->compv[i].sock && sa_isset(&raddr[i], SA_ALL))
				err |= icem_add_chan(m->icem, i+1, &raddr[i]);
		}
	}

	return err;
}


/** This can be called several times */
static int update(struct mnat_sess *sess)
{
	struct le *le;
	int err = 0;

	/* SDP session */
	(void)sdp_session_rattr_apply(sess->sdp, NULL, sdp_attr_handler, sess);

	/* SDP medialines */
	for (le = sess->medial.head; le; le = le->next) {
		struct mnat_media *m = le->data;

		sdp_media_rattr_apply(m->sdpm, NULL, media_attr_handler, m);
	}

	/* 5.1.  Verifying ICE Support */
	if (verify_peer_ice(sess)) {
		err = ice_start(sess);
	}
	else if (ice.turn) {
		info("ice: ICE not supported by peer, fallback to TURN\n");
		err = enable_turn_channels(sess);
	}
	else {
		info("ice: ICE not supported by peer\n");

		LIST_FOREACH(&sess->medial, le) {
			struct mnat_media *m = le->data;

			err |= set_media_attributes(m);
		}
	}

	return err;
}


static int module_init(void)
{
#ifdef MODULE_CONF
	struct pl pl;

	conf_get_bool(conf_cur(), "ice_turn", &ice.turn);
	conf_get_bool(conf_cur(), "ice_debug", &ice.debug);

	if (!conf_get(conf_cur(), "ice_nomination", &pl)) {
		if (0 == pl_strcasecmp(&pl, "regular"))
			ice.nom = ICE_NOMINATION_REGULAR;
		else if (0 == pl_strcasecmp(&pl, "aggressive"))
			ice.nom = ICE_NOMINATION_AGGRESSIVE;
		else {
			warning("ice: unknown nomination: %r\n", &pl);
			return EINVAL;
		}
	}
	if (!conf_get(conf_cur(), "ice_mode", &pl)) {
		if (!pl_strcasecmp(&pl, "full"))
			ice.mode = ICE_MODE_FULL;
		else if (!pl_strcasecmp(&pl, "lite"))
			ice.mode = ICE_MODE_LITE;
		else {
			warning("ice: unknown mode: %r\n", &pl);
			return EINVAL;
		}
	}
#endif

	return mnat_register(&mnat, baresip_mnatl(),
			     "ice", "+sip.ice",
			     session_alloc, media_alloc, update);
}


static int module_close(void)
{
	mnat = mem_deref(mnat);

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(ice) = {
	"ice",
	"mnat",
	module_init,
	module_close,
};
