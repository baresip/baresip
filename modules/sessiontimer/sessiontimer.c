/**
 * @file sessiontimer.c Session Timer module (RFC 4028)
 *
 * Copyright (C) 2025
 */
#include <string.h>
#include <re.h>
#include <baresip.h>

/**
 * Session Timer module implementing RFC 4028
 *
 * Uses UA events and sipsess_sock_set_hooks() (422 retry via resp422).
 * Session-Expires on outgoing INVITE uses call custom headers; in-dialog
 * messages use sipsess_set_hdrs().  On incoming INVITE with Supported:
 * timer but no Session-Expires, the UAS offers timers per RFC 4028 §8.4.
 * Refresher selection (sessiontimer_refresher = auto|uac|uas):
 * Initial INVITE (caller): config value, or omit refresher if auto.
 * Initial 2xx (callee): config value; if auto and INVITE empty → uas; if auto
 * and INVITE has refresher → mirror it.
 * In-dialog refresh request (initiator): always refresher=uac.
 * In-dialog 2xx (responder): mirror request; if request omits refresher, use
 * the same rules as the initial callee 2xx above.
 */

#define MIN_SESSION_INTERVAL 90
#define DEFAULT_SESSION_INTERVAL 1800
#define REFRESH_FACTOR 2

enum st_refresher {
	ST_REF_NONE = 0,
	ST_REF_UAC,
	ST_REF_UAS,
};

enum st_refresher_pref {
	ST_REF_PREF_AUTO = 0,  /* UAS decides when request omits refresher */
	ST_REF_PREF_UAC,
	ST_REF_PREF_UAS,
};

struct sessiontimer {
	struct le le;
	struct call *call;
	struct tmr tmr;
	struct tmr defer_tmr;
	uint32_t session_interval;
	/* Local policy Min-SE (what we advertise in Min-SE header). */
	uint32_t min_se;
	/* Peer Min-SE learned from messages (e.g. 422 Min-SE). */
	uint32_t peer_min_se;
	uint64_t session_expires;
	enum st_refresher refresher;
	uint32_t pending_interval;
	enum st_refresher pending_refresher;
	bool is_refresher;
	bool active;
	bool pending_restart;
	uint32_t retry_count;
};

static struct list sessiontimers;

static uint32_t default_min_se = MIN_SESSION_INTERVAL;
static uint32_t default_session_interval = DEFAULT_SESSION_INTERVAL;
static bool module_enabled = true;
static enum st_refresher_pref refresher_pref = ST_REF_PREF_AUTO;


/* Session-Expires must not be below Min-SE (RFC 4028). */
static uint32_t clamp_session_interval(uint32_t interval, uint32_t min_se)
{
	if (!interval)
		return 0;

	if (interval < MIN_SESSION_INTERVAL)
		interval = MIN_SESSION_INTERVAL;

	if (min_se && interval < min_se) {
		debug("sessiontimer: raising interval %u to Min-SE %u\n",
		      interval, min_se);
		interval = min_se;
	}

	return interval;
}


static void reload_sessiontimer_config(void)
{
	uint32_t interval = DEFAULT_SESSION_INTERVAL;
	uint32_t min_se = MIN_SESSION_INTERVAL;
	char refbuf[32];
	int err;

	err = conf_get_u32(conf_cur(), "sessiontimer_interval", &interval);
	if (err) {
		warning("sessiontimer: sessiontimer_interval missing in config, "
			"using %u seconds\n", DEFAULT_SESSION_INTERVAL);
		interval = DEFAULT_SESSION_INTERVAL;
	}
	else if (interval < MIN_SESSION_INTERVAL) {
		warning("sessiontimer: sessiontimer_interval %u too low, "
			"using %u seconds\n", interval, MIN_SESSION_INTERVAL);
		interval = MIN_SESSION_INTERVAL;
	}

	err = conf_get_u32(conf_cur(), "sessiontimer_min_se", &min_se);
	if (err)
		min_se = MIN_SESSION_INTERVAL;
	else if (min_se < MIN_SESSION_INTERVAL)
		min_se = MIN_SESSION_INTERVAL;

	default_min_se = min_se;
	default_session_interval = clamp_session_interval(interval, min_se);

	refresher_pref = ST_REF_PREF_AUTO;
	if (!conf_get_str(conf_cur(), "sessiontimer_refresher",
			  refbuf, sizeof(refbuf))) {
		if (!str_casecmp(refbuf, "uac"))
			refresher_pref = ST_REF_PREF_UAC;
		else if (!str_casecmp(refbuf, "uas"))
			refresher_pref = ST_REF_PREF_UAS;
		else
			refresher_pref = ST_REF_PREF_AUTO;
	}
}


/* RFC 4028 §9: UAS MAY reduce Session-Expires in 2xx but MUST NOT go
 * below Min-SE from the request (or 90 seconds). */
static uint32_t uas_answer_interval(uint32_t invite_interval,
				    uint32_t invite_min_se)
{
	uint32_t interval = invite_interval;

	if (!interval)
		return 0;

	if (interval > default_session_interval) {
		debug("sessiontimer: shortening interval %u to %u\n",
		      interval, default_session_interval);
		interval = default_session_interval;
	}

	if (invite_min_se && interval < invite_min_se)
		interval = invite_min_se;

	if (interval < MIN_SESSION_INTERVAL)
		interval = MIN_SESSION_INTERVAL;

	return clamp_session_interval(interval,
				      invite_min_se ? invite_min_se : default_min_se);
}


static void timer_ext_enable_all(void)
{
	struct le *le;

	for (le = list_head(uag_list()); le; le = le->next)
		ua_add_extension(le->data, "timer");
}


static void timer_ext_disable_all(void)
{
	struct le *le;

	for (le = list_head(uag_list()); le; le = le->next)
		ua_remove_extension(le->data, "timer");
}


static void parse_msg_session_params(const struct sip_msg *msg,
				     uint32_t *interval,
				     uint32_t *min_se,
				     enum st_refresher *refresher);

static void tmr_handler(void *arg);
static void negotiate_from_msg(struct sessiontimer *st,
			       const struct sip_msg *msg, bool request,
			       bool restart_timer);
static void update_session_timer(struct sessiontimer *st,
				 uint32_t session_interval,
				 enum st_refresher refresher);
static void schedule_timer_restart(struct sessiontimer *st,
				   uint32_t interval,
				   enum st_refresher refresher);
static void start_session_timer(struct sessiontimer *st);
static size_t format_session_headers(char *hdrs, size_t sz,
				     uint32_t session_interval,
				     uint32_t min_se, enum st_refresher refresher,
				     bool require_timer, bool include_supported);
static void invite_headers(struct call *call, uint32_t session_interval,
			   uint32_t min_se, enum st_refresher refresher,
			   bool require_timer);
static void sess_headers(struct call *call, uint32_t session_interval,
			 uint32_t min_se, enum st_refresher refresher,
			 bool require_timer);
static void uas_negotiate(struct sessiontimer *st, const struct sip_msg *msg,
			  bool peer_refresh);
static void prepare_answer_headers(struct sessiontimer *st);
static struct sessiontimer *alloc_timer(struct call *call);


static int hdr_text_copy(const struct sip_msg *msg, const char *name,
			 char *buf, size_t bufsz)
{
	const struct sip_hdr *hdr;

	if (!msg || !name || !buf || !bufsz)
		return EINVAL;

	buf[0] = '\0';

	if (!msg->hdrht)
		return ENOENT;

	hdr = sip_msg_xhdr(msg, name);
	if (!hdr)
		return ENOENT;

	if (!hdr->val.p || !hdr->val.l)
		return ENOENT;

	if (hdr->val.l >= bufsz)
		return EOVERFLOW;

	memcpy(buf, hdr->val.p, hdr->val.l);
	buf[hdr->val.l] = '\0';

	return 0;
}


static int parse_session_expires_str(const char *s, uint32_t *delta_seconds,
				     enum st_refresher *refresher)
{
	uint32_t delta = 0;
	enum st_refresher ref = ST_REF_NONE;
	const char *p;

	if (!s || !*s)
		return EINVAL;

	p = s;
	while (*p == ' ' || *p == '\t')
		++p;

	while (*p >= '0' && *p <= '9') {
		delta = delta * 10u + (uint32_t)(*p - '0');
		++p;
	}

	if (delta < MIN_SESSION_INTERVAL)
		return EINVAL;

	if (strstr(s, "refresher=uac"))
		ref = ST_REF_UAC;
	else if (strstr(s, "refresher=uas"))
		ref = ST_REF_UAS;

	if (delta_seconds)
		*delta_seconds = delta;
	if (refresher)
		*refresher = ref;

	return 0;
}


static int parse_min_se_str(const char *s, uint32_t *min_se)
{
	uint32_t mse = 0;
	const char *p;

	if (!s || !*s || !min_se)
		return EINVAL;

	p = s;
	while (*p == ' ' || *p == '\t')
		++p;

	while (*p >= '0' && *p <= '9') {
		mse = mse * 10u + (uint32_t)(*p - '0');
		++p;
	}

	if (mse < MIN_SESSION_INTERVAL)
		return EINVAL;

	*min_se = mse;
	return 0;
}


static bool msg_supports_timer(const struct sip_msg *msg)
{
	return msg && sip_msg_hdr_has_value(msg, SIP_HDR_SUPPORTED, "timer");
}


/* Caller Session-Expires refresher on initial INVITE (and INVITE retries). */
static enum st_refresher caller_offer_hdr_refresher(void)
{
	switch (refresher_pref) {
	case ST_REF_PREF_UAC:  return ST_REF_UAC;
	case ST_REF_PREF_UAS:  return ST_REF_UAS;
	default:               return ST_REF_NONE;
	}
}


/* Responder Session-Expires refresher: initial 2xx and in-dialog 2xx to refresh. */
static enum st_refresher responder_answer_hdr_refresher(
	enum st_refresher req_refresher)
{
	if (req_refresher != ST_REF_NONE)
		return req_refresher;

	switch (refresher_pref) {
	case ST_REF_PREF_UAC:  return ST_REF_UAC;
	case ST_REF_PREF_UAS:  return ST_REF_UAS;
	default:               return ST_REF_UAS;
	}
}


static struct sessiontimer *find_timer(const struct call *call)
{
	struct le *le;

	if (!call)
		return NULL;

	LIST_FOREACH(&sessiontimers, le) {
		struct sessiontimer *st = le->data;

		if (st->call == call)
			return st;
	}

	return NULL;
}


static bool refresher_is_local(const struct sessiontimer *st)
{
	if (!st)
		return false;

	if (call_is_outgoing(st->call))
		return st->refresher == ST_REF_UAC;

	return st->refresher == ST_REF_UAS;
}


static const char *refresher_param(enum st_refresher ref)
{
	switch (ref) {

	case ST_REF_UAC:  return "uac";
	case ST_REF_UAS:  return "uas";
	default:          return "uac";
	}
}


static enum st_refresher local_refresher(const struct sessiontimer *st)
{
	if (call_is_outgoing(st->call))
		return ST_REF_UAC;

	return ST_REF_UAS;
}


/* Header value on outbound session refresh requests. */
static enum st_refresher outbound_refresh_hdr_refresher(
	const struct sessiontimer *st)
{
	(void)st;

	/* On the wire, the refresh initiator is always the UAC of that request. */
	return ST_REF_UAC;
}


/* Session-Expires refresher in 2xx to a peer refresh request. */
static enum st_refresher peer_refresh_response_hdr_refresher(
	const struct sessiontimer *st, enum st_refresher req_hdr_refresher)
{
	(void)st;

	return responder_answer_hdr_refresher(req_hdr_refresher);
}


/* Dialog refresher role: wire refresh uses uac; do not flip negotiated role. */
static enum st_refresher dialog_refresher_role(
	const struct sessiontimer *st, enum st_refresher hdr_refresher)
{
	(void)hdr_refresher;

	if (st->refresher != ST_REF_NONE)
		return st->refresher;

	switch (refresher_pref) {
	case ST_REF_PREF_UAC:  return ST_REF_UAC;
	case ST_REF_PREF_UAS:  return ST_REF_UAS;
	default:
		return local_refresher(st);
	}
}


/* Dialog refresher after a peer refresh. */
static enum st_refresher dialog_refresher_from_peer_refresh(
	const struct sessiontimer *st, enum st_refresher hdr_refresher)
{
	return dialog_refresher_role(st, hdr_refresher);
}


/* Dialog refresher after our refresh gets a 2xx. */
static enum st_refresher refresh_2xx_dialog_refresher(
	const struct sessiontimer *st, enum st_refresher hdr_refresher)
{
	return dialog_refresher_role(st, hdr_refresher);
}


static void refresh_timer(struct sessiontimer *st);


static void refresh_timer(struct sessiontimer *st)
{
	uint64_t refresh_time;

	if (!st->active || !st->is_refresher)
		return;

	refresh_time = (st->session_interval * 1000) / REFRESH_FACTOR;
	st->session_expires = tmr_jiffies() + (st->session_interval * 1000);

	debug("sessiontimer: scheduling refresh in %u seconds "
	      "(expires in %u)\n",
	      (uint32_t)(refresh_time / 1000), st->session_interval);

	tmr_start(&st->tmr, refresh_time, tmr_handler, st);
}


static void destructor(void *arg)
{
	struct sessiontimer *st = arg;

	tmr_cancel(&st->defer_tmr);
	tmr_cancel(&st->tmr);
	list_unlink(&st->le);
}


static void start_session_timer(struct sessiontimer *st);


static void defer_work_handler(void *arg)
{
	struct sessiontimer *st = arg;
	uint32_t interval;
	enum st_refresher refresher;

	if (!st)
		return;

	if (!st->pending_restart)
		return;

	interval = st->pending_interval;
	refresher = st->pending_refresher;
	st->pending_restart = false;

	debug("sessiontimer: restart timer interval=%u refresher=%s\n",
	      interval, refresher_param(refresher));

	update_session_timer(st, interval, refresher);
	st->retry_count = 0;
	start_session_timer(st);
}


static void schedule_timer_restart(struct sessiontimer *st,
				   uint32_t interval,
				   enum st_refresher refresher)
{
	if (!st)
		return;

	st->pending_interval = interval;
	st->pending_refresher = refresher;
	st->pending_restart = true;
	tmr_start(&st->defer_tmr, 1, defer_work_handler, st);
}


static void handle_refresh_2xx_response(struct sessiontimer *st,
					const struct sip_msg *msg)
{
	uint32_t interval = 0;
	uint32_t min_se = 0;
	enum st_refresher refresher = ST_REF_NONE;
	uint32_t restart_iv;
	enum st_refresher restart_ref;

	if (!st || !msg || call_state(st->call) != CALL_STATE_ESTABLISHED)
		return;

	debug("sessiontimer: refresh 2xx %u %r\n", msg->scode, &msg->cseq.met);

	parse_msg_session_params(msg, &interval, &min_se, &refresher);

	if (interval) {
		uint32_t effective_min = st->min_se;

		if (min_se > st->peer_min_se)
			st->peer_min_se = min_se;
		if (st->peer_min_se > effective_min)
			effective_min = st->peer_min_se;
		if (min_se > effective_min)
			effective_min = min_se;

		interval = clamp_session_interval(interval, effective_min);
		restart_iv = interval;
		restart_ref = refresh_2xx_dialog_refresher(st, refresher);
	}
	else if (st->session_interval) {
		debug("sessiontimer: no Session-Expires in 2xx, keep "
		      "interval=%u\n", st->session_interval);
		restart_iv = st->session_interval;
		restart_ref = st->refresher != ST_REF_NONE ?
			      st->refresher : local_refresher(st);
	}
	else {
		return;
	}

	schedule_timer_restart(st, restart_iv, restart_ref);
}


static void update_session_timer(struct sessiontimer *st,
				 uint32_t session_interval,
				 enum st_refresher refresher)
{
	if (!st)
		return;

	{
		uint32_t effective_min = st->min_se;

		if (st->peer_min_se > effective_min)
			effective_min = st->peer_min_se;

		session_interval = clamp_session_interval(session_interval,
							effective_min);
	}

	st->session_interval = session_interval;
	st->refresher = refresher;
	st->is_refresher = refresher_is_local(st);
	st->active = true;
	st->session_expires = tmr_jiffies() + (session_interval * 1000);

	debug("sessiontimer: interval=%u refresher=%s local=%s\n",
	      session_interval, refresher_param(refresher),
	      st->is_refresher ? "yes" : "no");
}


static void start_session_timer(struct sessiontimer *st)
{
	uint32_t margin;

	if (!st || !st->active)
		return;

	if (st->is_refresher) {
		refresh_timer(st);
		return;
	}

	margin = st->session_interval / 3;

	if (margin > 32)
		margin = 32;

	if (margin >= st->session_interval)
		margin = st->session_interval / 2;

	debug("sessiontimer: waiting for peer refresh, margin %u s\n",
	      margin);
	tmr_start(&st->tmr,
		  (st->session_interval - margin) * 1000,
		  tmr_handler, st);
}


static enum st_refresher default_refresher_peer_request(const struct call *call)
{
	/* RFC 4028: no refresher in a request → requestor refreshes. */
	return call_is_outgoing(call) ? ST_REF_UAS : ST_REF_UAC;
}


static enum st_refresher default_refresher_established_2xx(
	const struct call *call)
{
	/* 2xx to INVITE without refresher: assume answering UAS refreshes. */
	return call_is_outgoing(call) ? ST_REF_UAS : ST_REF_UAC;
}


static void negotiate_from_msg(struct sessiontimer *st,
			       const struct sip_msg *msg, bool request,
			       bool restart_timer)
{
	uint32_t session_interval = 0;
	uint32_t min_se = 0;
	enum st_refresher refresher = ST_REF_NONE;

	if (!st || !msg)
		return;

	parse_msg_session_params(msg, &session_interval, &min_se, &refresher);

	if (!session_interval)
		return;

	if (min_se > st->peer_min_se)
		st->peer_min_se = min_se;

	{
		uint32_t effective_min = st->min_se;

		if (st->peer_min_se > effective_min)
			effective_min = st->peer_min_se;
		if (min_se > effective_min)
			effective_min = min_se;

		session_interval = clamp_session_interval(session_interval,
							effective_min);
	}

	if (refresher == ST_REF_NONE) {
		if (request)
			refresher = default_refresher_peer_request(st->call);
		else
			refresher = default_refresher_established_2xx(st->call);
	}

	if (!restart_timer && st->session_interval &&
	    st->session_interval != session_interval) {
		debug("sessiontimer: peer negotiated interval=%u "
		      "(proposed %u)\n",
		      session_interval, st->session_interval);
	}

	update_session_timer(st, session_interval, refresher);

	if (restart_timer)
		schedule_timer_restart(st, session_interval, refresher);
}


static void uas_negotiate(struct sessiontimer *st, const struct sip_msg *msg,
			  bool peer_refresh)
{
	uint32_t invite_interval = 0;
	uint32_t invite_min_se = 0;
	uint32_t session_interval;
	enum st_refresher hdr_ref = ST_REF_NONE;
	enum st_refresher dialog_ref;
	bool local_offer = false;
	char hdrs[384];
	size_t n;

	if (!st || !msg)
		return;

	parse_msg_session_params(msg, &invite_interval, &invite_min_se,
				 &hdr_ref);

	if (invite_min_se > st->peer_min_se)
		st->peer_min_se = invite_min_se;

	if (!invite_interval) {
		if (peer_refresh) {
			debug("sessiontimer: peer %r without Session-Expires, "
			      "skip\n", &msg->met);
			return;
		}

		if (!msg_supports_timer(msg))
			return;

		/* RFC 4028 §8.4: caller supports timers but did not
		 * request an interval — UAS may offer one in the 2xx. */
		invite_interval = default_session_interval;
		local_offer = true;
		debug("sessiontimer: offering interval=%u (Supported: timer, "
		      "no Session-Expires)\n", invite_interval);
	}

	session_interval = uas_answer_interval(invite_interval, invite_min_se);
	if (!session_interval)
		return;

	if (!peer_refresh)
		hdr_ref = responder_answer_hdr_refresher(hdr_ref);

	if (peer_refresh) {
		enum st_refresher resp_hdr =
			peer_refresh_response_hdr_refresher(st, hdr_ref);

		dialog_ref = dialog_refresher_from_peer_refresh(st, hdr_ref);

		n = format_session_headers(hdrs, sizeof(hdrs), session_interval,
					   0, resp_hdr, false, false);
		if (!n) {
			warning("sessiontimer: format peer refresh headers "
				"failed\n");
			return;
		}

		debug("sessiontimer: peer %r interval=%u hdr=%s dialog=%s\n",
		      &msg->met, session_interval, refresher_param(resp_hdr),
		      refresher_param(dialog_ref));

		sipsess_set_hdrs(call_sipsess(st->call), "%b", hdrs, n);
		schedule_timer_restart(st, session_interval, dialog_ref);
	}
	else {
		/* RFC 4028: Min-SE MUST NOT be used in 2xx responses */
		sess_headers(st->call, session_interval, 0, hdr_ref,
			     hdr_ref == ST_REF_UAC);
		update_session_timer(st, session_interval, hdr_ref);
	}
}


static struct call *call_from_sess(struct sipsess *sess)
{
	return sess ? (struct call *)sipsess_arg(sess) : NULL;
}


static int hdr_prep_handler(struct sipsess *sess, void *arg)
{
	struct call *call;
	struct sessiontimer *st;
	const struct sip_msg *msg;
	uint32_t invite_interval = 0;
	uint32_t invite_min_se = 0;
	enum st_refresher ref = ST_REF_NONE;
	(void)arg;

	call = call_from_sess(sess);
	if (!call || call_is_outgoing(call))
		return 0;

	st = find_timer(call);
	if (!st) {
		st = alloc_timer(call);
		if (!st)
			return 0;

		st->refresher = ST_REF_UAS;
		st->is_refresher = false;
	}

	/* Strict RFC 4028 UAS behavior:
	 * If the INVITE proposes a Session-Expires below our local policy
	 * minimum, reject with 422 and include Min-SE. */
	msg = sipsess_msg(sess);
	if (msg) {
		parse_msg_session_params(msg, &invite_interval, &invite_min_se,
					 &ref);

		if (invite_interval && invite_interval < default_min_se) {
			(void)sipsess_set_hdrs(sess, "Min-SE: %u\r\n",
					       default_min_se);
			mem_deref(st);
			return 422;
		}
	}

	prepare_answer_headers(st);
	return 0;
}


static void target_refresh_handler(struct sipsess *sess,
				   const struct sip_msg *msg, void *arg)
{
	struct call *call;
	struct sessiontimer *st;
	(void)arg;

	if (!sess || !msg)
		return;

	call = call_from_sess(sess);
	if (!call)
		return;

	st = find_timer(call);
	if (!st) {
		debug("sessiontimer: target refresh without timer\n");
		return;
	}

	uas_negotiate(st, msg, true);
}


static void refresh_2xx_handler(struct sipsess *sess,
				 const struct sip_msg *msg, void *arg)
{
	struct call *call;
	struct sessiontimer *st;
	(void)arg;

	if (!sess || !msg)
		return;

	call = call_from_sess(sess);
	if (!call)
		return;

	st = find_timer(call);
	if (!st)
		return;

	handle_refresh_2xx_response(st, msg);
}


static size_t format_session_headers(char *hdrs, size_t sz,
				     uint32_t session_interval,
				     uint32_t min_se, enum st_refresher refresher,
				     bool require_timer, bool include_supported)
{
	size_t n = 0;

	if (!hdrs || !sz)
		return 0;

	if (include_supported) {
		n += re_snprintf(hdrs + n, sz - n,
				 "Supported: timer\r\n");
	}

	if (session_interval > 0) {
		if (refresher != ST_REF_NONE) {
			n += re_snprintf(hdrs + n, sz - n,
					 "Session-Expires: %u;refresher=%s\r\n",
					 session_interval,
					 refresher_param(refresher));
		}
		else {
			n += re_snprintf(hdrs + n, sz - n,
					 "Session-Expires: %u\r\n",
					 session_interval);
		}
	}

	if (min_se > 0 && min_se >= MIN_SESSION_INTERVAL) {
		n += re_snprintf(hdrs + n, sz - n,
				 "Min-SE: %u\r\n", min_se);
	}

	if (require_timer) {
		n += re_snprintf(hdrs + n, sz - n,
				 "Require: timer\r\n");
	}

	return n;
}


static void invite_headers(struct call *call, uint32_t session_interval,
			   uint32_t min_se, enum st_refresher refresher,
			   bool require_timer)
{
	int err;

	if (!call)
		return;

	call_custom_hdr_remove(call, "Session-Expires");
	call_custom_hdr_remove(call, "Min-SE");
	call_custom_hdr_remove(call, "Require");

	if (session_interval > 0) {
		if (refresher != ST_REF_NONE) {
			err = call_custom_hdr_add(call, "Session-Expires",
						  "%u;refresher=%s",
						  session_interval,
						  refresher_param(refresher));
		}
		else {
			err = call_custom_hdr_add(call, "Session-Expires",
						  "%u",
						  session_interval);
		}
		if (err)
			warning("sessiontimer: Session-Expires: %m\n", err);
	}

	if (min_se > 0 && min_se >= MIN_SESSION_INTERVAL) {
		err = call_custom_hdr_add(call, "Min-SE", "%u", min_se);
		if (err)
			warning("sessiontimer: Min-SE: %m\n", err);
	}

	if (require_timer) {
		err = call_custom_hdr_add(call, "Require", "timer");
		if (err)
			warning("sessiontimer: Require: %m\n", err);
	}
}


static void sess_headers(struct call *call, uint32_t session_interval,
			 uint32_t min_se, enum st_refresher refresher,
			 bool require_timer)
{
	char hdrs[384];
	size_t n;
	struct sipsess *sess;

	if (!call)
		return;

	sess = call_sipsess(call);
	if (!sess)
		return;

	n = format_session_headers(hdrs, sizeof(hdrs), session_interval, min_se,
				   refresher, require_timer,
				   call_is_outgoing(call));
	if (!n)
		return;

	if (sipsess_set_hdrs(sess, "%b", hdrs, n))
		warning("sessiontimer: set session headers failed\n");
}


static void parse_msg_session_params(const struct sip_msg *msg,
				     uint32_t *interval,
				     uint32_t *min_se,
				     enum st_refresher *refresher)
{
	char sebuf[128];
	char msebuf[64];
	int err;

	if (interval)
		*interval = 0;
	if (min_se)
		*min_se = 0;
	if (refresher)
		*refresher = ST_REF_NONE;

	err = hdr_text_copy(msg, "Session-Expires", sebuf, sizeof(sebuf));

	if (!err) {
		uint32_t se = 0;
		enum st_refresher ref = ST_REF_NONE;

		if (!parse_session_expires_str(sebuf, &se, &ref)) {
			if (interval)
				*interval = se;
			if (refresher)
				*refresher = ref;
		}
	}

	err = hdr_text_copy(msg, "Min-SE", msebuf, sizeof(msebuf));
	if (!err && min_se) {
		uint32_t mse = 0;

		if (!parse_min_se_str(msebuf, &mse))
			*min_se = mse;
	}
}


static struct sessiontimer *alloc_timer(struct call *call)
{
	struct sessiontimer *st;

	st = mem_zalloc(sizeof(*st), destructor);
	if (!st)
		return NULL;

	st->call = call;
	st->min_se = default_min_se;
	st->peer_min_se = 0;
	tmr_init(&st->tmr);
	tmr_init(&st->defer_tmr);
	list_append(&sessiontimers, &st->le, st);

	return st;
}


static int handle_422_response(struct sessiontimer *st,
			       const struct sip_msg *msg)
{
	char msebuf[64];
	uint32_t peer_min_se = 0;
	int err;

	if (!st || !msg)
		return ENOSYS;

	err = hdr_text_copy(msg, "Min-SE", msebuf, sizeof(msebuf));
	if (err)
		return EINVAL;

	err = parse_min_se_str(msebuf, &peer_min_se);
	if (err || !peer_min_se)
		return EINVAL;

	if (peer_min_se > st->peer_min_se) {
		st->peer_min_se = peer_min_se;
		debug("sessiontimer: 422 Min-SE=%u\n", peer_min_se);
	}

	st->retry_count++;
	if (st->retry_count > 5) {
		warning("sessiontimer: too many 422 retries, giving up\n");
		call_hangup(st->call, 422, "Session Interval Too Small");
		mem_deref(st);
		return EINVAL;
	}

	/* Raise Session-Expires to satisfy peer minimum. Keep advertising our
	 * local Min-SE policy in Min-SE header on the retry. */
	{
		uint32_t effective_min = st->min_se;

		if (st->peer_min_se > effective_min)
			effective_min = st->peer_min_se;

		st->session_interval = clamp_session_interval(st->session_interval,
							      effective_min);
	}

	debug("sessiontimer: retry with interval=%u\n", st->session_interval);

	/* RFC 4028: before the dialog is established, the UAC retry should
	 * include Min-SE set to the largest Min-SE observed in 422s for this
	 * Call-ID. */
	{
		uint32_t advertised_min_se = st->min_se;
		if (st->peer_min_se > advertised_min_se)
			advertised_min_se = st->peer_min_se;

		invite_headers(st->call, st->session_interval, advertised_min_se,
			       caller_offer_hdr_refresher(), false);
	}

	if (call_is_outgoing(st->call) &&
	    call_state(st->call) != CALL_STATE_ESTABLISHED) {
		err = call_refresh_outgoing_hdrs(st->call);
		if (err)
			return err;
	}
	else {
		sess_headers(st->call, st->session_interval, st->min_se,
			     outbound_refresh_hdr_refresher(st), false);
	}

	return 0;
}


static int resp422_handler(struct sipsess *sess, const struct sip_msg *msg,
			   void *arg)
{
	struct call *call;
	struct sessiontimer *st;
	(void)arg;

	call = call_from_sess(sess);
	if (!call)
		return EINVAL;

	st = find_timer(call);
	if (!st)
		return ENOTSUP;

	return handle_422_response(st, msg);
}


static void activate_on_established(struct sessiontimer *st)
{
	enum st_refresher ref;
	const struct sip_msg *msg;

	if (!st || !st->call)
		return;

	debug("sessiontimer: call established\n");

	/* UAC: parse Session-Expires from 200 OK after media is up.
	 * Must not run from sipsess_answer_handler (reentrancy crash). */
	if (call_is_outgoing(st->call)) {
		msg = call_msg(st->call);
		if (msg)
			negotiate_from_msg(st, msg, false, false);
	}

	/* RFC 4028: learned Min-SE max from 422s is cleared once established */
	st->peer_min_se = 0;

	if (st->active) {
		start_session_timer(st);
		return;
	}

	if (st->session_interval < MIN_SESSION_INTERVAL) {
		if (call_is_outgoing(st->call))
			st->session_interval = default_session_interval;
		else
			return;
	}

	ref = st->refresher;
	if (ref == ST_REF_NONE)
		ref = call_is_outgoing(st->call) ? ST_REF_UAC : ST_REF_UAS;

	update_session_timer(st, st->session_interval, ref);
	start_session_timer(st);
}


static void prepare_answer_headers(struct sessiontimer *st)
{
	const struct sip_msg *msg;

	if (!st || !st->call)
		return;

	reload_sessiontimer_config();

	msg = call_msg(st->call);
	if (msg)
		uas_negotiate(st, msg, false);
}


static void tmr_handler(void *arg)
{
	struct sessiontimer *st = arg;
	int err;

	if (!st || !st->call)
		return;

	if (tmr_jiffies() >= st->session_expires) {
		warning("sessiontimer: session expired, sending BYE\n");
		call_hangup(st->call, 408, "Session Timer Expired");
		mem_deref(st);
		return;
	}

	if (!st->is_refresher) {
		uint32_t remain;

		remain = (uint32_t)(st->session_expires - tmr_jiffies());
		if (remain > 5000)
			remain = 5000;

		tmr_start(&st->tmr, remain, tmr_handler, st);
		return;
	}

	if (!call_refresh_allowed(st->call)) {
		debug("sessiontimer: refresh blocked, retry in 5s\n");
		tmr_start(&st->tmr, 5000, tmr_handler, st);
		return;
	}

	info("sessiontimer: sending refresh (interval=%u)\n",
	     st->session_interval);
	sess_headers(st->call, st->session_interval, st->min_se,
		     outbound_refresh_hdr_refresher(st), false);
	err = call_modify(st->call);
	if (err) {
		warning("sessiontimer: refresh failed: %m\n", err);
		tmr_start(&st->tmr, 5000, tmr_handler, st);
	}
	else {
		st->retry_count = 0;
		debug("sessiontimer: refresh sent, awaiting 2xx\n");
	}
}


static void event_handler(enum ua_event ev, struct bevent *event, void *arg)
{
	struct call *call = bevent_get_call(event);
	struct sessiontimer *st;
	(void)arg;
	(void)event;

	switch (ev) {

	case UA_EVENT_CREATE:
		if (!module_enabled)
			break;

		ua_add_extension(bevent_get_ua(event), "timer");
		break;

	case UA_EVENT_CALL_OUTGOING:
		if (!call)
			break;

		if (find_timer(call))
			break;

		reload_sessiontimer_config();

		st = alloc_timer(call);
		if (!st)
			break;

		st->session_interval = clamp_session_interval(
			default_session_interval, st->min_se);
		{
			enum st_refresher hdr = caller_offer_hdr_refresher();

			st->refresher = hdr;
			st->is_refresher = hdr != ST_REF_NONE &&
					   refresher_is_local(st);
			if (hdr == ST_REF_NONE) {
				debug("sessiontimer: propose interval=%u "
				      "(no refresher) on INVITE\n",
				      st->session_interval);
			}
			else {
				debug("sessiontimer: propose interval=%u "
				      "refresher=%s on INVITE\n",
				      st->session_interval,
				      refresher_param(hdr));
			}
			invite_headers(call, st->session_interval, st->min_se,
				       hdr, false);
		}
		break;

	case UA_EVENT_CALL_INCOMING:
		if (!call)
			break;

		if (find_timer(call))
			break;

		reload_sessiontimer_config();

		st = alloc_timer(call);
		if (!st)
			break;

		st->session_interval = 0;
		st->refresher = ST_REF_UAS;
		st->is_refresher = false;
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		st = find_timer(call);
		if (st)
			activate_on_established(st);
		break;

	case UA_EVENT_CALL_REMOTE_SDP:
		break;

	case UA_EVENT_CALL_CLOSED:
		st = find_timer(call);
		if (st) {
			debug("sessiontimer: call closed\n");
			mem_deref(st);
		}
		break;

	default:
		break;
	}
}


static int module_init(void)
{
	int err;

	list_init(&sessiontimers);

	conf_get_bool(conf_cur(), "sessiontimer_enable", &module_enabled);
	if (!module_enabled) {
		info("sessiontimer: disabled by config\n");
		return 0;
	}

	reload_sessiontimer_config();

	timer_ext_enable_all();

	err = bevent_register(event_handler, NULL);
	if (err)
		goto out;

	sipsess_sock_set_hooks(uag_sipsess_sock(), hdr_prep_handler,
			       target_refresh_handler, refresh_2xx_handler,
			       resp422_handler, NULL);

	info("sessiontimer: loaded (interval=%u, min=%u)\n",
	     default_session_interval, default_min_se);

	return 0;

 out:
	timer_ext_disable_all();
	return err;
}


static int module_close(void)
{
	debug("sessiontimer: module closing..\n");

	bevent_unregister(event_handler);
	sipsess_sock_unset_hooks(uag_sipsess_sock());
	timer_ext_disable_all();

	if (!list_isempty(&sessiontimers)) {
		debug("sessiontimer: flushing %u timers\n",
		     list_count(&sessiontimers));
		list_flush(&sessiontimers);
	}

	return 0;
}


const struct mod_export DECL_EXPORTS(sessiontimer) = {
	"sessiontimer",
	"application",
	module_init,
	module_close
};
