/**
 * @file src/ua.c  User-Agent
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"


/** Magic number */
#define MAGIC 0x0a0a0a0a
#include "magic.h"


/** Defines a SIP User Agent object */
struct ua {
	MAGIC_DECL                   /**< Magic number for struct ua         */
	struct le le;                /**< Linked list element                */
	struct account *acc;         /**< Account Parameters                 */
	struct list regl;            /**< List of Register clients           */
	struct list calls;           /**< List of active calls (struct call) */
	struct pl extensionv[8];     /**< Vector of SIP extensions           */
	size_t    extensionc;        /**< Number of SIP extensions           */
	char *cuser;                 /**< SIP Contact username               */
	char *pub_gruu;              /**< SIP Public GRUU                    */
	enum presence_status pstat;  /**< Presence Status                    */
	struct list hdr_filter;      /**< Filter for incoming headers        */
	struct list custom_hdrs;     /**< List of outgoing headers           */
	char *ansval;                /**< SIP auto answer value              */
	struct sa dst;               /**< Current destination address        */
};

struct ua_xhdr_filter {
	struct le le;
	char *hdr_name;
};


static void ua_destructor(void *arg)
{
	struct ua *ua = arg;
	struct le *le;

	list_unlink(&ua->le);

	if (!list_isempty(&ua->regl))
		bevent_ua_emit(BEVENT_UNREGISTERING, ua, NULL);

	LIST_FOREACH(&ua->calls, le) {
		struct call *call = le->data;
		bevent_call_emit(BEVENT_CALL_CLOSED, call,
				 "User-Agent deleted");
	}

	list_flush(&ua->calls);
	list_flush(&ua->regl);
	mem_deref(ua->cuser);
	mem_deref(ua->pub_gruu);
	mem_deref(ua->ansval);
	mem_deref(ua->acc);

	if (uag_delayed_close() && list_isempty(uag_list())) {
		sip_close(uag_sip(), false);
	}

	list_flush(&ua->custom_hdrs);
	list_flush(&ua->hdr_filter);
}


void ua_printf(const struct ua *ua, const char *fmt, ...)
{
	va_list ap;

	if (!ua)
		return;

	va_start(ap, fmt);
	info("%r@%r: %v", &ua->acc->luri.user, &ua->acc->luri.host, fmt, &ap);
	va_end(ap);
}


void ua_add_extension(struct ua *ua, const char *extension)
{
	struct pl e;

	if (ua->extensionc >= RE_ARRAY_SIZE(ua->extensionv)) {
		warning("ua: maximum %zu number of SIP extensions\n",
			RE_ARRAY_SIZE(ua->extensionv));
		return;
	}

	pl_set_str(&e, extension);

	ua->extensionv[ua->extensionc++] = e;
}


void ua_remove_extension(struct ua *ua, const char *extension)
{
	size_t i;
	int found = 0;

	for (i = 0; i < ua->extensionc; i++) {
		if (found) {
			ua->extensionv[i-1] = ua->extensionv[i];
			continue;
		}

		if (!pl_strcmp(&ua->extensionv[i], extension))
			found = 1;
	}

	ua->extensionc -= found;
}


static int create_register_clients(struct ua *ua)
{
	int err = 0;

	/* Register clients */
	if (uag_cfg() && str_isset(uag_cfg()->uuid))
		ua_add_extension(ua, "gruu");

	if (0 == str_casecmp(ua->acc->sipnat, "outbound")) {

		size_t i;

		ua_add_extension(ua, "path");
		ua_add_extension(ua, "outbound");

		if (!str_isset(uag_cfg()->uuid)) {

			warning("ua: outbound requires valid UUID!\n");
			err = ENOSYS;
			goto out;
		}

		for (i=0; i<RE_ARRAY_SIZE(ua->acc->outboundv); i++) {

			if (ua->acc->outboundv[i] && ua->acc->regint) {
				err = reg_add(&ua->regl, ua, (int)i+1);
				if (err)
					break;
			}
		}
	}
	else if (ua->acc->regint) {
		err = reg_add(&ua->regl, ua, 0);
	}

	ua_add_extension(ua, "replaces");
	ua_add_extension(ua, "norefersub");

	if (ua->acc->rel100_mode)
		ua_add_extension(ua, "100rel");

 out:
	return err;
}


static int start_register(struct ua *ua, bool fallback)
{
	struct account *acc;
	struct le *le;
	struct uri uri;
	char *reg_uri = NULL;
	char params[256] = "";
	unsigned i;
	int err;

	if (!ua)
		return EINVAL;

	acc = ua->acc;

	if (acc->regint == 0)
		return 0;

	uri = ua->acc->luri;
	uri.user = pl_null;

	err = re_sdprintf(&reg_uri, "%H", uri_encode, &uri);
	if (err)
		goto out;

	if (uag_cfg() && str_isset(uag_cfg()->uuid)) {
		if (re_snprintf(params, sizeof(params),
				";+sip.instance=\"<urn:uuid:%s>\"",
				uag_cfg()->uuid) < 0) {
			err = ENOMEM;
			goto out;
		}
	}

	if (acc->regq) {
		if (re_snprintf(&params[strlen(params)],
				sizeof(params) - strlen(params),
				";q=%s", acc->regq) < 0) {
			err = ENOMEM;
			goto out;
		}
	}

	if (acc->mnat && acc->mnat->ftag) {
		if (re_snprintf(&params[strlen(params)],
				sizeof(params) - strlen(params),
				";%s", acc->mnat->ftag) < 0) {
			err = ENOMEM;
			goto out;
		}
	}

	if (list_isempty(&ua->regl))
		create_register_clients(ua);

	if (!fallback && !list_isempty(&ua->regl))
		bevent_ua_emit(BEVENT_REGISTERING, ua, NULL);

	for (le = ua->regl.head, i=0; le; le = le->next, i++) {
		struct reg *reg = le->data;

		if (!list_isempty(&ua->custom_hdrs))
			reg_set_custom_hdrs(reg, &ua->custom_hdrs);

		err = reg_register(reg, reg_uri, params,
				   fallback ? 0 : acc->regint,
				   acc->outboundv[i]);
		if (err) {
			warning("ua: SIP%s register failed: %m\n",
					fallback ? " fallback" : "", err);

			bevent_ua_emit(fallback ?
				       BEVENT_FALLBACK_FAIL :
				       BEVENT_REGISTER_FAIL,
				       ua, "%m", err);
			goto out;
		}
	}

 out:
	mem_deref(reg_uri);

	return err;
}


/**
 * Start registration of a User-Agent
 *
 * @param ua User-Agent
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_register(struct ua *ua)
{
	if (!ua)
		return EINVAL;

	debug("ua: ua_register %s\n", account_aor(ua->acc));

	return start_register(ua, false);
}


/**
 * Start fallback registration checks (Cisco-keep-alive) of a User-Agent. These
 * are in the sense of RFC3261 REGISTER requests with expire set to zero. A
 * SIP proxy will handle this as un-register and send a 200 OK. Then the UA is
 * not registered but it knows that the SIP proxy is available and can be used
 * as fallback proxy.
 *
 * @param ua User-Agent
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_fallback(struct ua *ua)
{
	if (!ua || !ua_account(ua)->fbregint)
		return 0;

	debug("ua: ua_fallback %s\n", account_aor(ua->acc));

	return start_register(ua, true);
}


/**
 * Stop all register clients of a User-Agent
 *
 * @param ua User-Agent
 */
void ua_stop_register(struct ua *ua)
{
	struct le *le;

	if (!ua)
		return;

	if (!list_isempty(&ua->regl))
		bevent_ua_emit(BEVENT_UNREGISTERING, ua, NULL);

	for (le = ua->regl.head; le; le = le->next) {
		struct reg *reg = le->data;

		reg_stop(reg);
	}
}


/**
 * Unregister all Register clients of a User-Agent
 *
 * @param ua User-Agent
 */
void ua_unregister(struct ua *ua)
{
	struct le *le;

	if (!ua)
		return;

	if (!list_isempty(&ua->regl))
		bevent_ua_emit(BEVENT_UNREGISTERING, ua, NULL);

	for (le = ua->regl.head; le; le = le->next) {
		struct reg *reg = le->data;

		reg_unregister(reg);
	}
}


/**
 * Check if a User-Agent is registered
 *
 * @param ua User-Agent object
 *
 * @return True if registered, false if not registered
 */
bool ua_isregistered(const struct ua *ua)
{
	struct le *le;

	if (!ua)
		return false;

	for (le = ua->regl.head; le; le = le->next) {

		const struct reg *reg = le->data;

		/* it is enough if one of the registrations work */
		if (reg_isok(reg))
			return true;
	}

	return false;
}


/**
 * Check if last User-Agent registration failed
 *
 * @param ua User-Agent object
 *
 * @return True if registration failed
 */
bool ua_regfailed(const struct ua *ua)
{
	struct le *le;
	bool failed = true;

	if (!ua)
		return false;

	for (le = ua->regl.head; le; le = le->next) {

		const struct reg *reg = le->data;

		/* both registration failed? */
		failed &= reg_failed(reg);
	}

	return failed;
}


bool ua_reghasladdr(const struct ua *ua, const struct sa *laddr)
{
	struct le *le;

	if (!ua || !laddr)
		return false;

	for (le = ua->regl.head; le; le = le->next) {

		const struct reg *reg = le->data;
		if (sa_cmp(reg_laddr(reg), laddr, SA_ADDR))
			return true;
	}

	return false;
}


/**
 * Destroy the user-agent, terminate all active calls and
 * send the SHUTDOWN event.
 *
 * @param ua User-Agent object
 *
 * @return Number of remaining references
 */
unsigned ua_destroy(struct ua *ua)
{
	unsigned nrefs;

	if (!ua)
		return 0;

	list_unlink(&ua->le);

	/* send the shutdown event */
	bevent_ua_emit(BEVENT_SHUTDOWN, ua, NULL);

	/* terminate all calls now */
	list_flush(&ua->calls);

	/* number of remaining references (excluding this one) */
	nrefs = mem_nrefs(ua) - 1;

	mem_deref(ua);

	return nrefs;
}


struct call *ua_find_call_onhold(const struct ua *ua)
{
	struct le *le;

	if (!ua)
		return NULL;

	for (le = ua->calls.tail; le; le = le->prev) {

		struct call *call = le->data;

		if (call_is_onhold(call))
			return call;
	}

	return NULL;
}


/**
 * Find call of a User-Agent with given call state
 *
 * @param ua  User-Agent
 * @param st  Call-state
 *
 * @return The call if found, otherwise NULL.
 */
struct call *ua_find_call_state(const struct ua *ua, enum call_state st)
{
	struct le *le;

	if (!ua)
		return NULL;

	for (le = ua->calls.tail; le; le = le->prev) {

		struct call *call = le->data;

		if (call_state(call) == st)
			return call;
	}

	return NULL;
}


struct call *ua_find_active_call(struct ua *ua)
{
	struct le *le = NULL;

	if (!ua)
		return NULL;

	for (le = list_head(&ua->calls); le; le = le->next) {
		struct call *call = le->data;
		if (call_state(call) == CALL_STATE_ESTABLISHED &&
			!call_is_onhold(call))
			return call;
	}

	return NULL;
}


struct call *ua_find_call_msg(struct ua *ua, const struct sip_msg *msg)
{
	if (!ua || !msg)
		return NULL;

	struct le *le = NULL;
	for (le = list_tail(&ua->calls); le; le = le->prev) {
		struct call *call = le->data;
		if (call_sess_cmp(call, msg))
			break;
	}

	return le ? le->data : NULL;
}


static void call_event_handler(struct call *call, enum call_event ev,
			       const char *str, void *arg)
{
	struct ua *ua = arg;
	const char *peeruri;

	MAGIC_CHECK(ua);

	peeruri = call_peeruri(call);
	if (!peeruri)
		return;

	switch (ev) {

	case CALL_EVENT_INCOMING:

		if (contact_block_access(baresip_contacts(),
					 peeruri)) {

			info("ua: blocked access: \"%s\"\n", peeruri);

			bevent_call_emit(BEVENT_CALL_CLOSED, call,
					 "%s", str);
			mem_deref(call);
			break;
		}

		bevent_call_emit(BEVENT_CALL_INCOMING, call, "%s", peeruri);
		switch (ua->acc->answermode) {

		case ANSWERMODE_EARLY:
		case ANSWERMODE_EARLY_AUDIO:
			(void)call_progress(call);
			break;

		case ANSWERMODE_EARLY_VIDEO:
			if (!call_early_video_available(call)) {
				info ("ua: peer is not capable of early "
					"video. proceed as normal call\n");
				break;
			}

			(void)call_progress(call);
			break;

		default:
			break;
		}
		break;

	case CALL_EVENT_RINGING:
		bevent_call_emit(BEVENT_CALL_RINGING, call, "%s", peeruri);
		break;

	case CALL_EVENT_OUTGOING:
		bevent_call_emit(BEVENT_CALL_OUTGOING, call, "%s", peeruri);
		break;

	case CALL_EVENT_PROGRESS:
		ua_printf(ua, "Call in-progress: %s\n", peeruri);
		bevent_call_emit(BEVENT_CALL_PROGRESS, call, "%s", peeruri);
		break;

	case CALL_EVENT_ANSWERED:
		ua_printf(ua, "Call answered: %s\n", peeruri);
		bevent_call_emit(BEVENT_CALL_ANSWERED, call, "%s", peeruri);
		break;

	case CALL_EVENT_ESTABLISHED:
		ua_printf(ua, "Call established: %s\n", peeruri);
		bevent_call_emit(BEVENT_CALL_ESTABLISHED, call,
				"%s", peeruri);
		break;

	case CALL_EVENT_CLOSED:
		bevent_call_emit(BEVENT_CALL_CLOSED, call, "%s", str);
		mem_deref(call);
		break;

	case CALL_EVENT_TRANSFER:
		bevent_call_emit(BEVENT_CALL_TRANSFER, call, "%s", str);
		break;

	case CALL_EVENT_TRANSFER_FAILED:
		bevent_call_emit(BEVENT_CALL_TRANSFER_FAILED, call,
				"%s", str);
		break;

	case CALL_EVENT_MENC:
		bevent_call_emit(BEVENT_CALL_MENC, call, "%s", str);
		break;
	}
}


static void call_dtmf_handler(struct call *call, char key, void *arg)
{
	struct ua *ua = arg;
	char key_str[2];

	MAGIC_CHECK(ua);

	if (key != KEYCODE_NONE && key != KEYCODE_REL) {

		key_str[0] = key;
		key_str[1] = '\0';

		bevent_call_emit(BEVENT_CALL_DTMF_START, call,
				 "%s", key_str);
	}
	else {
		bevent_call_emit(BEVENT_CALL_DTMF_END, call, NULL);
	}
}


static bool require_handler(const struct sip_hdr *hdr,
			    const struct sip_msg *msg, void *arg)
{
	struct ua *ua = arg;
	bool supported = false;
	size_t i;
	(void)msg;

	for (i=0; i<ua->extensionc; i++) {

		if (!pl_casecmp(&hdr->val, &ua->extensionv[i])) {
			supported = true;
			break;
		}
	}

	return !supported;
}


static int sdp_connection(const struct mbuf *mb, int *af, struct sa *sa)
{
	struct pl pl1, pl2;
	const struct network *net = baresip_network();

	*af = AF_UNSPEC;

	int err = re_regex((char *)mbuf_buf(mb), mbuf_get_left(mb),
			   "c=IN IP[46]1 [^ \r\n]+", &pl1, &pl2);
	if (err)
		return err;

	switch (pl1.p[0]) {

	case '4':
		*af = AF_INET;
		break;

	case '6':
		*af = AF_INET6;
		break;

	default:
		return EAFNOSUPPORT;
	}

	/* OSX/iOS needs a port number for udp_connect() */
	err = re_regex((char *)mbuf_buf(mb), mbuf_get_left(mb),
		       "m=audio [0-9]+ ", &pl1);
	if (err)
		err = re_regex((char *)mbuf_buf(mb), mbuf_get_left(mb),
				"m=video [0-9]+ ", &pl1);

	if (err)
		goto out;

	err = sa_set(sa, &pl2, pl_u32(&pl1));
	if (sa_af(sa) == AF_INET6 && sa_is_linklocal(sa))
		err |= net_set_dst_scopeid(net, sa);

out:
	if (!err && !sa_isset(sa, SA_ADDR))
	    return EINVAL;

	return err;
}


/* Handle incoming calls */
void sipsess_conn_handler(const struct sip_msg *msg, void *arg)
{
	struct config *config = conf_config();
	const char magic_branch[] = RE_RFC3261_BRANCH_ID;
	const struct sip_hdr *hdr;
	struct ua *ua;

	(void)arg;

	debug("ua: sipsess connect via %s %J --> %J\n",
	      sip_transp_name(msg->tp),
	      &msg->src, &msg->dst);

	if (pl_strncmp(&msg->via.branch, magic_branch, sizeof(magic_branch)-1)
	    != 0) {
		info("ua: received INVITE with incorrect Via branch.\n");
		(void)sip_treply(NULL, uag_sip(), msg, 606, "Not Acceptable");
		return;
	}

	ua = uag_find_msg(msg);
	if (!ua) {
		info("ua: %r: UA not found: %H\n",
		     &msg->from.auri, uri_encode, &msg->uri);
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return;
	}

	if (!ua_req_check_origin(ua, msg)) {
		(void)sip_treply(NULL, uag_sip(), msg, 403, "Forbidden");
		return;
	}

	/* handle multiple calls */
	if (config->call.max_calls &&
	    uag_call_count() + 1 > config->call.max_calls) {

		info("ua: rejected call from %r (maximum %d calls)\n",
		     &msg->from.auri, config->call.max_calls);
		(void)sip_treply(NULL, uag_sip(), msg, 486, "Max Calls");
		return;
	}

	/* Handle Require: header, check for any required extensions */
	hdr = sip_msg_hdr_apply(msg, true, SIP_HDR_REQUIRE,
				require_handler, ua);
	if (hdr) {
		info("ua: call from %r rejected with 420"
			     " -- option-tag '%r' not supported\n",
			     &msg->from.auri, &hdr->val);

		(void)sip_treplyf(NULL, NULL, uag_sip(), msg, false,
				  420, "Bad Extension",
				  "Unsupported: %r\r\n"
				  "Content-Length: 0\r\n\r\n",
				  &hdr->val);
		return;
	}

	if (ua->acc->rel100_mode == REL100_REQUIRED &&
	    !(sip_msg_hdr_has_value(msg, SIP_HDR_SUPPORTED, "100rel") ||
	      sip_msg_hdr_has_value(msg, SIP_HDR_REQUIRE, "100rel"))) {

		info("ua: call from %r rejected with 421"
			     " -- option-tag '%s' not supported by remote\n",
			     &msg->from.auri, "100rel");
		(void)sip_treplyf(NULL, NULL, uag_sip(), msg, false,
				  421, "Extension required",
				  "Require: 100rel\r\n"
				  "Content-Length: 0\r\n\r\n");
	}

	if (config->call.accept)
		(void)ua_accept(ua, msg);
	else
		bevent_sip_msg_emit(BEVENT_SIPSESS_CONN, msg,
				    "incoming call");
}


/**
 * Accept an incoming call
 *
 * @param ua        User-agent
 * @param msg       SIP message of incoming call
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_accept(struct ua *ua, const struct sip_msg *msg)
{
	struct call *call = NULL;
	char *to_uri = NULL;
	int err;

	if (!ua || !msg)
		return EINVAL;

	if (ua_find_call_msg(ua, msg)) {
		warning("ua: call was already created\n");
		return EINVAL;
	}

	err = pl_strdup(&to_uri, &msg->to.auri);
	if (err)
		goto error;

	err = ua_call_alloc(&call, ua, VIDMODE_ON, msg, NULL, to_uri, true);
	if (err) {
		warning("ua: call_alloc: %m\n", err);
		goto error;
	}

	if (!list_isempty(&ua->hdr_filter)) {
		struct list hdrs;
		struct le *le;

		list_init(&hdrs);

		le = list_head(&ua->hdr_filter);
		while (le) {
			const struct sip_hdr *tmp_hdr;
			const struct ua_xhdr_filter *filter = le->data;

			le = le->next;
			tmp_hdr = sip_msg_xhdr(msg, filter->hdr_name);

			if (tmp_hdr) {
				char name[256];

				pl_strcpy(&tmp_hdr->name, name, sizeof(name));
				if (custom_hdrs_add(&hdrs, name,
						    "%r", &tmp_hdr->val))
					goto error;
			}
		}

		call_set_custom_hdrs(call, &hdrs);
		list_flush(&hdrs);
	}

	err = call_accept(call, uag_sipsess_sock(), msg);
	if (err)
		goto error;

	mem_deref(to_uri);
	return 0;

 error:
	mem_deref(call);
	mem_deref(to_uri);
	(void)sip_treply(NULL, uag_sip(), msg, 500, "Call Error");
	return err;
}


static const struct sa *ua_regladdr(const struct ua *ua)
{
	struct le *le;
	size_t i;

	for (le = ua->regl.head, i=0; le; le = le->next, i++) {
		const struct reg *reg = le->data;
		if (reg_isok(reg))
			return reg_laddr(reg);
	}

	return NULL;
}


/**
 * Create a new call object
 *
 * @param callp     Pointer to allocated call object
 * @param ua        User-agent
 * @param vmode     Wanted video mode
 * @param msg       SIP message for incoming calls
 * @param xcall     Optional call to inherit properties from
 * @param local_uri Local SIP uri
 * @param use_rtp   Use RTP flag
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_call_alloc(struct call **callp, struct ua *ua,
		  enum vidmode vmode, const struct sip_msg *msg,
		  struct call *xcall, const char *local_uri,
		  bool use_rtp)
{
	const struct network *net = baresip_network();
	struct call_prm cprm;
	int af = AF_UNSPEC;
	struct sa dst;
	const struct sa *laddr = NULL;
	int err;

	if (!callp || !ua)
		return EINVAL;

	sa_init(&dst, AF_UNSPEC);
	if (msg && !sdp_connection(msg->mb, &af, &dst)) {
		info("ua: using connection-address %j of SDP offer\n", &dst);
		sa_cpy(&ua->dst, &dst);
	}
	else if (sa_isset(&ua->dst, SA_ADDR)) {
		af = sa_af(&ua->dst);
	}
	else if (msg) {
		laddr = &msg->dst;
		af = sa_af(laddr);
	}
	else if (ua->acc->regint) {
		laddr = ua_regladdr(ua);
		af = sa_af(laddr);
	}

	if (af != AF_UNSPEC && !net_af_enabled(net, af)) {
		warning("ua: address family %s not supported\n",
				net_af2name(af));
		(void)sip_treply(NULL, uag_sip(), msg, 488,
				 "Not Acceptable Here");
		return EINVAL;
	}

	memset(&cprm, 0, sizeof(cprm));

	if (sa_isset(laddr, SA_ADDR)) {
		sa_cpy(&cprm.laddr, laddr);
	}
	else if (sa_isset(&ua->dst, SA_ADDR)) {
		laddr = net_laddr_for(net, &ua->dst);
		if (!sa_isset(laddr, SA_ADDR)) {
			warning("ua: no laddr for %j\n", &ua->dst);
			sa_init(&ua->dst, AF_UNSPEC);
			return EINVAL;
		}

		sa_init(&ua->dst, AF_UNSPEC);
		sa_cpy(&cprm.laddr, laddr);
	}

	cprm.vidmode = vmode;
	cprm.af      = af;
	cprm.use_rtp = use_rtp;

	err = call_alloc(callp, conf_config(), &ua->calls,
			 ua->acc->dispname,
			 local_uri ? local_uri : ua->acc->aor,
			 ua->acc, ua, &cprm,
			 msg, xcall,
			 net_dnsc(net),
			 call_event_handler, ua);
	if (err)
		return err;

	if (!list_isempty(&ua->custom_hdrs))
		call_set_custom_hdrs(*callp, &ua->custom_hdrs);

	call_set_handlers(*callp, NULL, call_dtmf_handler, ua);

	return 0;
}


void ua_handle_options(struct ua *ua, const struct sip_msg *msg)
{
	struct sip_contact contact;
	struct call *call = NULL;
	struct mbuf *desc = NULL;
	const struct sip_hdr *hdr;
	bool accept_sdp = true;
	int err;

	debug("ua: incoming OPTIONS message from %r (%J)\n",
	      &msg->from.auri, &msg->src);

	/* application/sdp is the default if the
	   Accept header field is not present */
	hdr = sip_msg_hdr(msg, SIP_HDR_ACCEPT);
	if (hdr) {
		accept_sdp = 0==pl_strcasecmp(&hdr->val, "application/sdp");
	}

	if (accept_sdp) {

		err = ua_call_alloc(&call, ua, VIDMODE_ON, msg, NULL, NULL,
				    false);
		if (err) {
			(void)sip_treply(NULL, uag_sip(), msg,
					 500, "Call Error");
			return;
		}

		err = call_streams_alloc(call);
		if (err)
			return;

		err = call_sdp_get(call, &desc, true);
		if (err)
			goto out;
	}

	sip_contact_set(&contact, ua_cuser(ua), &msg->dst, msg->tp);

	err = sip_treplyf(NULL, NULL, uag_sip(),
			  msg, true, 200, "OK",
			  "Allow: %H\r\n"
			  "%H"
			  "%H"
			  "%s"
			  "Content-Length: %zu\r\n"
			  "\r\n"
			  "%b",
			  ua_print_allowed, ua,
			  ua_print_supported, ua,
			  sip_contact_print, &contact,
			  desc ? "Content-Type: application/sdp\r\n" : "",
			  desc ? mbuf_get_left(desc) : (size_t)0,
			  desc ? mbuf_buf(desc) : NULL,
			  desc ? mbuf_get_left(desc) : (size_t)0);
	if (err) {
		warning("ua: reply to OPTIONS failed (%m)\n", err);
	}

 out:
	mem_deref(desc);
	mem_deref(call);
}


static int uas_authorization(uint8_t *ha1, const struct pl *user,
			     const char *realm, void *arg)
{
	struct ua *ua = arg;
	struct account *acc = ua_account(ua);

	if (pl_strcasecmp(user, acc->uas_user))
		return EAUTH;

	return md5_printf(ha1, "%r:%s:%s", user, realm, acc->uas_pass);
}


/**
 * Request and perform authorization of an incoming request if necessary
 *
 * @param ua   Pointer to User-Agent object
 * @param msg  SIP message
 *
 * @return 0 if request is authorized, otherwise errorcode
 */
int uas_req_auth(struct ua *ua, const struct sip_msg *msg)
{
	struct sip_uas_auth auth;
	char realm[32];
	int err;
	struct account *acc = ua_account(ua);
	struct uri *uri = account_luri(acc);

	re_snprintf(realm, sizeof(realm), "%r@%r", &uri->user, &uri->host);
	auth.realm = realm;

	err = sip_uas_auth_check(&auth, msg, uas_authorization, ua);
	switch (err) {
	case 0:

	break;
	case EAUTH: {
		int err2;
		struct sip_uas_auth *auth2;
		debug("ua: %r Unauthorized for %s\n", &msg->met, auth.realm);
		err2 = sip_uas_auth_gen(&auth2, msg, auth.realm);
		if (err2)
			return err2;

		(void)sip_replyf(uag_sip(), msg, 401, "Unauthorized",
				"%H"
				"Content-Length: 0\r\n"
				"\r\n", sip_uas_auth_print, auth2);
		mem_deref(auth2);
	}

	break;
	default:
		info("ua: %r forbidden for %s\n", &msg->met, auth.realm);
		(void)sip_reply(uag_sip(), msg, 403, "Forbidden");
	break;
	}

	return err;
}


bool ua_handle_refer(struct ua *ua, const struct sip_msg *msg)
{
	struct sip_contact contact;
	const struct sip_hdr *hdr;
	bool sub = true;
	int err;

	debug("ua: incoming REFER message from %r (%J)\n",
	      &msg->from.auri, &msg->src);

	/* application/sdp is the default if the
	   Accept header field is not present */
	hdr = sip_msg_hdr(msg, SIP_HDR_REFER_SUB);
	if (hdr)
		pl_bool(&sub, &hdr->val);

	if (sub) {
		debug("ua: out of dialog REFER with subscription not "
			"supported by %s\n", __func__);
		return false;
	}

	/* get the transfer target */
	hdr = sip_msg_hdr(msg, SIP_HDR_REFER_TO);
	if (!hdr) {
		warning("call: bad REFER request from %r\n", &msg->from.auri);
		(void)sip_reply(uag_sip(), msg, 400,
				"Missing Refer-To header");
		return true;
	}

	err = uas_req_auth(ua, msg);
	if (err)
		goto out;

	sip_contact_set(&contact, ua_cuser(ua), &msg->dst, msg->tp);
	err = sip_treplyf(NULL, NULL, uag_sip(),
			  msg, true, 202, "Accepted",
			  "%H"
			  "Refer-Sub: false\r\n"
			  "Content-Length: 0\r\n"
			  "\r\n",
			  sip_contact_print, &contact);
	if (err ) {
		warning("ua: reply to REFER failed (%m)\n", err);
		goto out;
	}

	debug("ua: REFER to %r\n", &hdr->val);
	bevent_ua_emit(BEVENT_REFER, ua, "%r", &hdr->val);

out:

	return true;
}


static const char *autoans_header_name(enum answer_method met)
{
	switch (met) {

	case ANSM_RFC5373:   return "Answer-Mode";
	case ANSM_CALLINFO:  return "Call-Info";
	case ANSM_ALERTINFO: return "Alert-Info";
	default: return NULL;
	}
}


static bool user_cmp_handler(struct le *le, void *arg)
{
	struct ua *ua = le->data;
	struct pl *user = arg;

	return pl_cmp(&ua->acc->luri.user, user) == 0;
}


static int ua_cuser_gen(struct ua *ua)
{
	bool suffix = false;
	int err;

	conf_get_bool(conf_cur(), "sip_cuser_random", &suffix);

	suffix |= list_apply(uag_list(), true, user_cmp_handler,
			     &ua->acc->luri.user) != NULL;
	if (suffix) {
		char buf[16];
		rand_str(buf, sizeof(buf));
		err = re_sdprintf(&ua->cuser, "%r-%s", &ua->acc->luri.user,
				  buf);
	}
	else {
		err = pl_strdup(&ua->cuser, &ua->acc->luri.user);
	}

	debug("ua: contact user %s\n", ua->cuser);
	return err;
}


/**
 * Allocate a SIP User-Agent
 *
 * @param uap   Pointer to allocated User-Agent object
 * @param aor   SIP Address-of-Record (AOR)
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_alloc(struct ua **uap, const char *aor)
{
	struct ua *ua;
	struct uri *luri;
	char *buf = NULL;
	char *host = NULL;
	int err;

	if (!aor)
		return EINVAL;

	ua = mem_zalloc(sizeof(*ua), ua_destructor);
	if (!ua)
		return ENOMEM;

	MAGIC_INIT(ua);

	list_init(&ua->calls);

	/* Decode SIP address */
	if (uag_eprm()) {
		err = re_sdprintf(&buf, "%s;%s", aor, uag_eprm());
		if (err)
			goto out;
		aor = buf;
	}

	err = account_alloc(&ua->acc, aor);
	if (err)
		goto out;

	err = ua_cuser_gen(ua);
	if (err)
		goto out;

	if (ua->acc->sipnat) {
		ua_printf(ua, "Using sipnat: '%s'\n", ua->acc->sipnat);
	}

	if (ua->acc->mnat) {
		ua_printf(ua, "Using medianat '%s'\n",
			  ua->acc->mnat->id);

		if (0 == str_casecmp(ua->acc->mnat->id, "ice"))
			ua_add_extension(ua, "ice");
	}

	if (ua->acc->menc) {
		ua_printf(ua, "Using media encryption '%s'\n",
			  ua->acc->menc->id);
	}

	if (ua->acc->cert) {
		err = sip_transp_add_ccert(uag_sip(),
			&ua->acc->laddr.uri, ua->acc->cert);
		if (err) {
			warning("ua: SIP/TLS add client "
				"certificate %s failed: %m\n",
				ua->acc->cert, err);
			return err;
		}

		luri = account_luri(ua->acc);
		if (luri) {
			err = pl_strdup(&host, &luri->host);
			if (err)
				goto out;
		}

		err = tls_add_certf(uag_tls(), ua->acc->cert, host);
		if (err) {
			warning("ua: SIP/TLS add server "
				"certificate %s failed: %m\n",
				ua->acc->cert, err);
			goto out;
		}
	}

	err = create_register_clients(ua);
	if (err)
		goto out;

	list_append(uag_list(), &ua->le, ua);
	bevent_ua_emit(BEVENT_CREATE, ua, "%s", account_aor(ua->acc));

 out:
	mem_deref(host);
	mem_deref(buf);
	if (err)
		mem_deref(ua);
	else if (uap)
		*uap = ua;

	return err;
}


/**
 * Update a User-agent object, reset register clients
 *
 * @param ua User-Agent object
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_update_account(struct ua *ua)
{
	if (!ua)
		return EINVAL;

	/* clear extensions and reg clients */
	ua->extensionc = 0;
	list_flush(&ua->regl);

	return create_register_clients(ua);
}


/**
 * Appends params to mbuf if params do not already exist in mbuf.
 *
 * @param mb mbuf to append to
 * @param params params in the form ;uri-param1;uri-param2
 *
 * @return 0 if success, otherwise errorcode
 */
static int mbuf_append_params(struct mbuf *mb, const struct pl *params)
{
	if (!mb || !params)
		return EINVAL;

	struct pl cur = *params;
	const char *e = cur.p + cur.l;
	struct pl par = PL_INIT;
	while (!re_regex(cur.p, cur.l, ";[ \t\r\n]*[~ \t\r\n=;]*",
			NULL, &par)) {
		char *buf;
		int err = pl_strdup(&buf, &par);
		if (err)
			return err;

		struct pl value = PL_INIT;
		(void)msg_param_decode(&cur, buf, &value);

		struct pl pl = {.p = (const char*) mb->buf, .l = mb->pos};
		if (msg_param_exists(&pl, buf, &par) != 0) {
			if (pl_isset(&value))
				err = mbuf_printf(mb, ";%r=%r", &par, &value);
			else
				err = mbuf_printf(mb, ";%r", &par);
		}

		mem_deref(buf);

		/* skip over the parameter */
		if (pl_isset(&value))
			cur.p = value.p + value.l;
		else
			cur.p = par.p + par.l;

		cur.l = e - cur.p;

		if (err)
			return err;
	}

	return 0;
}


/**
 * Connect an outgoing call to a given SIP uri with audio and video direction
 *
 * @param ua        User-Agent
 * @param callp     Optional pointer to allocated call object
 * @param from_uri  Optional From uri, or NULL for default AOR
 * @param req_uri   SIP uri to connect to
 * @param vmode     Video mode
 * @param adir      Audio media direction
 * @param vdir      Video media direction
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_connect_dir(struct ua *ua, struct call **callp,
	       const char *from_uri, const char *req_uri,
	       enum vidmode vmode, enum sdp_dir adir, enum sdp_dir vdir)
{
	struct call *call = NULL;
	const struct network *net = baresip_network();
	struct mbuf *dialbuf;
	struct sip_addr addr;
	struct pl pl;
	int err = 0;

	if (!ua || !str_isset(req_uri))
		return EINVAL;

	if (uag_nodial()) {
		info ("ua: currently no outgoing calls are allowed\n");
		return EACCES;
	}

	dialbuf = mbuf_alloc(64);
	if (!dialbuf)
		return ENOMEM;

	err = mbuf_write_str(dialbuf, req_uri);
	if (err)
		goto out;

	/* Append any optional URI parameters */
	err |= mbuf_append_params(dialbuf, &ua->acc->luri.params);
	if (err)
		goto out;

	mbuf_set_pos(dialbuf, 0);
	pl_set_mbuf(&pl, dialbuf);
	sa_init(&ua->dst, AF_UNSPEC);
	if (!sip_addr_decode(&addr, &pl))
		(void)sa_set(&ua->dst, &addr.uri.host, addr.uri.port);

	if (sa_isset(&ua->dst, SA_ADDR) && !sa_isset(&ua->dst, SA_PORT))
		sa_set_port(&ua->dst, SIP_PORT);

	if (sa_af(&ua->dst) == AF_INET6 && sa_is_linklocal(&ua->dst)) {
		err = net_set_dst_scopeid(net, &ua->dst);
		if (err)
			goto out;
	}

	err = ua_call_alloc(&call, ua, vmode, NULL, NULL, from_uri, true);
	if (err)
		goto out;

	if (adir != SDP_SENDRECV || vdir != SDP_SENDRECV)
		call_set_media_direction(call, adir, vdir);

	err = call_connect(call, &pl);

	if (err)
		mem_deref(call);
	else if (callp)
		*callp = call;

 out:
	mem_deref(dialbuf);

	return err;
}


/**
 * Connect an outgoing call to a given SIP uri
 *
 * @param ua        User-Agent
 * @param callp     Optional pointer to allocated call object
 * @param from_uri  Optional From uri, or NULL for default AOR
 * @param req_uri   SIP uri to connect to
 * @param vmode     Video mode
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_connect(struct ua *ua, struct call **callp,
	       const char *from_uri, const char *req_uri,
	       enum vidmode vmode)
{
	return ua_connect_dir(ua, callp, from_uri, req_uri, vmode,
		SDP_SENDRECV, SDP_SENDRECV);
}


/**
 * Hangup the current call
 *
 * @param ua     User-Agent
 * @param call   Call to hangup, or NULL for current call
 * @param scode  Optional status code
 * @param reason Optional reason
 */
void ua_hangup(struct ua *ua, struct call *call,
	       uint16_t scode, const char *reason)
{
	ua_hangupf(ua, call, scode, reason, NULL);
}


/**
 * Hangup the current call
 *
 * @param ua     User-Agent
 * @param call   Call to reject, or NULL for current call
 * @param scode  Optional status code
 * @param reason Optional reason
 * @param fmt    Formatted headers
 * @param ...    Variable arguments
 */
void ua_hangupf(struct ua *ua, struct call *call,
		uint16_t scode, const char *reason, const char *fmt, ...)
{
	if (!ua)
		return;

	if (!call) {
		call = ua_call(ua);
		if (!call)
			return;
	}

	va_list ap;
	va_start(ap, fmt);
	call_hangupf(call, scode, reason, fmt ? "%v" : NULL, fmt, &ap);
	va_end(ap);

	bevent_call_emit(BEVENT_CALL_CLOSED, call,
			 reason ? reason : "Rejected by user");

	mem_deref(call);
}


/**
 * Answer an incoming call
 *
 * @param ua    User-Agent
 * @param call  Call to answer, or NULL for current call
 * @param vmode Wanted video mode
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_answer(struct ua *ua, struct call *call, enum vidmode vmode)
{
	if (!ua)
		return EINVAL;

	if (!call) {
		call = ua_find_call_state(ua, CALL_STATE_INCOMING);
		if (!call)
			return ENOENT;
	}

	return call_answer(call, 200, vmode);
}


/**
 * Put the established call on hold and answer the given call
 *
 * @param ua    User-Agent
 * @param call  Call to answer, or NULL for last incoming call
 * @param vmode Wanted video mode for the incoming call
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_hold_answer(struct ua *ua, struct call *call, enum vidmode vmode)
{
	struct call *pcall;
	int err;

	if (!ua)
		return EINVAL;

	if (!call) {
		call = ua_find_call_state(ua, CALL_STATE_INCOMING);
		if (!call)
			return ENOENT;
	}

	/* put established call on-hold */
	pcall = ua_find_call_state(ua, CALL_STATE_ESTABLISHED);
	if (pcall) {
		ua_printf(ua, "putting call with '%s' on hold\n",
		     call_peeruri(pcall));

		err = call_hold(pcall, true);
		if (err)
			return err;
	}

	return ua_answer(ua, call, vmode);
}


/**
 * Print the user-agent registration status
 *
 * @param pf  Print function
 * @param ua  User-Agent object
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_print_status(struct re_printf *pf, const struct ua *ua)
{
	struct le *le;
	int err;

	if (!ua)
		return 0;

	err = re_hprintf(pf, "%-42s", ua->acc->aor);

	for (le = ua->regl.head; le; le = le->next)
		err |= reg_status(pf, le->data);

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Send SIP OPTIONS message to a peer
 *
 * @param ua      User-Agent object
 * @param uri     Peer SIP Address
 * @param resph   Response handler
 * @param arg     Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_options_send(struct ua *ua, const char *uri,
		    options_resp_h *resph, void *arg)
{
	int err = 0;

	if (!ua || !str_isset(uri))
		return EINVAL;

	err = sip_req_send(ua, "OPTIONS", uri, resph, arg,
			   "Accept: application/sdp\r\n"
			   "Content-Length: 0\r\n"
			   "\r\n");
	if (err) {
		warning("ua: send options: (%m)\n", err);
	}

	return err;
}


/**
 * Send SIP REFER message to a peer
 *
 * @param ua       User-Agent object
 * @param uri      Peer SIP Address
 * @param referto  Refer-To value
 * @param resph    Response handler
 * @param arg      Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_refer_send(struct ua *ua, const char *uri, const char *referto,
		  refer_resp_h *resph, void *arg)
{
	int err = 0;

	if (!ua || !str_isset(uri))
		return EINVAL;

	err = sip_req_send(ua, "REFER", uri, resph, arg,
			   "Contact: <%s>\r\n"
			   "%H"
			   "Refer-To: %s\r\n"
			   "Refer-Sub: false\r\n"
			   "Content-Length: 0\r\n"
			   "\r\n",
			   account_aor(ua_account(ua)),
			   ua_print_supported, ua,
			   referto);
	if (err) {
		warning("ua: send refer: (%m)\n", err);
	}

	return err;
}


/**
 * Get presence status of a User-Agent
 *
 * @param ua User-Agent object
 *
 * @return presence status
 */
enum presence_status ua_presence_status(const struct ua *ua)
{
	return ua ? ua->pstat : PRESENCE_UNKNOWN;
}


/**
 * Set presence status of a User-Agent
 *
 * @param ua     User-Agent object
 * @param status Presence status
 */
void ua_presence_status_set(struct ua *ua, enum presence_status status)
{
	if (!ua)
		return;

	ua->pstat = status;
}


/**
 * Get the outbound SIP proxy of a User-Agent
 *
 * @param ua User-Agent object
 *
 * @return Outbound SIP proxy uri
 */
const char *ua_outbound(const struct ua *ua)
{
	/* NOTE: we pick the first outbound server, should be rotated? */
	return ua ? ua->acc->outboundv[0] : NULL;
}


/**
 * Get the current call object of a User-Agent
 *
 * @param ua User-Agent object
 *
 * @return Current call, NULL if no active calls
 *
 *
 * Current call strategy:
 *
 * We can only have 1 current call. The current call is the one that was
 * added last (end of the list).
 */
struct call *ua_call(const struct ua *ua)
{
	if (!ua)
		return NULL;

	return list_ledata(list_tail(&ua->calls));
}


/**
 * Print the user-agent debug information
 *
 * @param pf  Print function
 * @param ua  User-Agent object
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_debug(struct re_printf *pf, const struct ua *ua)
{
	struct le *le;
	int err;

	if (!ua)
		return 0;

	err  = re_hprintf(pf, "--- %s ---\n", ua->acc->aor);
	err |= re_hprintf(pf, " nrefs:     %u\n", mem_nrefs(ua));
	err |= re_hprintf(pf, " cuser:     %s\n", ua->cuser);
	err |= re_hprintf(pf, " pub-gruu:  %s\n", ua->pub_gruu);
	err |= re_hprintf(pf, " %H", ua_print_supported, ua);

	err |= account_debug(pf, ua->acc);

	for (le = ua->regl.head; le; le = le->next)
		err |= reg_debug(pf, le->data);

	return err;
}


/**
 * Print the user-agent information in JSON
 *
 * @param od  User-Agent dict
 * @param ua  User-Agent object
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_state_json_api(struct odict *od, const struct ua *ua)
{
	struct odict *reg = NULL;
	struct odict *cfg = NULL;
	struct le *le;
	size_t i = 0;
	int err = 0;

	if (!ua)
		return 0;

	err |= odict_alloc(&reg, 8);
	err |= odict_alloc(&cfg, 8);

	/* user-agent info */
	err |= odict_entry_add(od, "cuser", ODICT_STRING, ua->cuser);

	/* account info */
	err |= account_json_api(od, cfg, ua->acc);
	if (err)
		warning("ua: failed to encode json account (%m)\n", err);

	/* registration info */
	for (le = list_head(&ua->regl); le; le = le->next) {
		struct reg *regm = le->data;
		err |= reg_json_api(reg, regm);
		++i;
	}
	if (i > 1)
		warning("ua: multiple registrations for one account");

	err |= odict_entry_add(reg, "interval", ODICT_INT,
			(int64_t) ua->acc->regint);
	err |= odict_entry_add(reg, "q_value", ODICT_DOUBLE, ua->acc->regq);

	if (err)
		warning("ua: failed to encode json registration (%m)\n", err);

	/* package */
	err |= odict_entry_add(od, "settings", ODICT_OBJECT, cfg);
	err |= odict_entry_add(od, "registration", ODICT_OBJECT, reg);
	if (err)
		warning("ua: failed to encode json package (%m)\n", err);

	mem_deref(cfg);
	mem_deref(reg);
	return err;
}


static void ua_xhdr_filter_destructor(void *arg)
{
	struct ua_xhdr_filter *filter = arg;
	mem_deref(filter->hdr_name);
}


/**
 * Add custom SIP header to filter for incoming calls
 *
 * @param ua       User-Agent
 * @param hdr_name SIP Header name
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_add_xhdr_filter(struct ua *ua, const char *hdr_name)
{
	struct ua_xhdr_filter *filter;

	if (!ua)
		return EINVAL;

	filter = mem_zalloc(sizeof(*filter), ua_xhdr_filter_destructor);
	if (!filter)
		return ENOMEM;

	if (str_dup(&filter->hdr_name, hdr_name)) {
		mem_deref(filter);
		return ENOMEM;
	}

	list_append(&ua->hdr_filter, &filter->le, filter);

	return 0;
}


/**
 * Print all calls for a given User-Agent
 *
 * @param pf     Print handler for debug output
 * @param ua     User-Agent
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_print_calls(struct re_printf *pf, const struct ua *ua)
{
	uint32_t n, count=0;
	uint32_t linenum;
	int err = 0;

	if (!ua) {
		err |= re_hprintf(pf, "\n--- No active calls ---\n");
		return err;
	}

	n = list_count(&ua->calls);

	err |= re_hprintf(pf, "\nUser-Agent: %r@%r\n",
			&ua->acc->luri.user, &ua->acc->luri.host);
	err |= re_hprintf(pf, "--- Active calls (%u) ---\n",
			  n);

	for (linenum=CALL_LINENUM_MIN; linenum<CALL_LINENUM_MAX; linenum++) {

		const struct call *call;

		call = call_find_linenum(&ua->calls, linenum);
		if (call) {
			++count;

			err |= re_hprintf(pf, "%s %H\n",
					  call == ua_call(ua) ? ">" : " ",
					  call_info, call);
		}

		if (count >= n)
			break;
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Get the contact user/uri of a User-Agent (UA)
 *
 * If the Public GRUU is set, it will be returned.
 * Otherwise the local contact-user (cuser) will be returned.
 *
 * @param ua User-Agent
 *
 * @return Contact user
 */
const char *ua_cuser(const struct ua *ua)
{
	if (!ua)
		return NULL;

	if (str_isset(ua->pub_gruu))
		return ua->pub_gruu;

	return ua->cuser;
}


/**
 * Get the local contact username
 *
 * @param ua User-Agent
 *
 * @return Local contact username
 */
const char *ua_local_cuser(const struct ua *ua)
{
	return ua ? ua->cuser : NULL;
}


/**
 * Get Account of a User-Agent
 *
 * @param ua User-Agent
 *
 * @return Pointer to UA's account
 */
struct account *ua_account(const struct ua *ua)
{
	return ua ? ua->acc : NULL;
}


/**
 * Set Public GRUU of a User-Agent (UA)
 *
 * @param ua   User-Agent
 * @param pval Public GRUU
 */
void ua_pub_gruu_set(struct ua *ua, const struct pl *pval)
{
	if (!ua)
		return;

	ua->pub_gruu = mem_deref(ua->pub_gruu);
	(void)pl_strdup(&ua->pub_gruu, pval);
}


/**
 * Print list of methods allowed by the UA
 *
 * @param pf  Print function
 * @param ua  User-Agent
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_print_allowed(struct re_printf *pf, const struct ua *ua)
{
	int err;

	if (!ua || !ua->acc)
		return 0;

	err = re_hprintf(pf,
			 "INVITE,ACK,BYE,CANCEL,OPTIONS,"
			 "NOTIFY,INFO,MESSAGE,UPDATE");

	if (uag_subh())
		err |= re_hprintf(pf, ",SUBSCRIBE");

	if (ua->acc->rel100_mode)
		err |= re_hprintf(pf, ",PRACK");

	if (ua->acc->refer)
		err |= re_hprintf(pf, ",REFER");

	return err;
}


/**
 * Print the supported extensions
 *
 * @param pf  Print function
 * @param ua  User-Agent object
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_print_supported(struct re_printf *pf, const struct ua *ua)
{
	size_t i;
	int err;

	if (!ua)
		return 0;

	err = re_hprintf(pf, "Supported:");

	for (i=0; i<ua->extensionc; i++) {
		err |= re_hprintf(pf, "%s%r",
				  i==0 ? " " : ",", &ua->extensionv[i]);
	}

	err |= re_hprintf(pf, "\r\n");

	return err;
}


/**
 * Print the required extensions
 *
 * @param pf  Print function
 * @param ua  User-Agent object
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_print_require(struct re_printf *pf, const struct ua *ua)
{
	int err = 0;

	if (!ua)
		return 0;

	if (ua->acc->rel100_mode == REL100_REQUIRED)
		err = re_hprintf(pf, "Require: 100rel\r\n");

	return err;
}


/**
 * Get the list of call objects
 *
 * @param ua User-Agent
 *
 * @return List of call objects (struct call)
 */
struct list *ua_calls(const struct ua *ua)
{
	return ua ? (struct list *)&ua->calls : NULL;
}


/**
 * Enable handling of all inbound requests, even if
 * the request uri is not matching.
 *
 * @param ua      User-Agent
 * @param enabled True to enable, false to disable
 */
void ua_set_catchall(struct ua *ua, bool enabled)
{
	if (!ua)
		return;

	account_set_catchall(ua_account(ua), enabled);
}


bool ua_catchall(struct ua *ua)
{
	return ua && ua->acc ? ua->acc->catchall : false;
}


/**
 * Add a custom SIP header
 *
 * @param ua     User-Agent
 * @param name   Custom SIP header name
 * @param value  Custom SIP header value
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_add_custom_hdr(struct ua *ua, const struct pl *name,
		      const struct pl *value)
{
	int err;
	char *buf;

	if (!ua || !name || !value)
		return EINVAL;

	err = pl_strdup(&buf, name);
	if (err)
		return err;

	err = custom_hdrs_add(&ua->custom_hdrs, buf, "%r", value);
	mem_deref(buf);
	if (err)
		return err;

	return 0;
}


/**
 * Remove a custom SIP header
 *
 * @param ua    User-Agent
 * @param name  Custom SIP header name
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_rm_custom_hdr(struct ua *ua, struct pl *name)
{
	struct le *le;

	if (!ua)
		return EINVAL;

	le = list_head(&ua->custom_hdrs);
	while (le) {
		struct sip_hdr *h = le->data;
		le = le->next;

		if (!pl_cmp(&h->name, name)) {
			list_unlink(&h->le);
			mem_deref(h);
		}
	}

	return 0;
}


/**
 * Set a list of custom SIP headers
 *
 * @param ua             User-Agent
 * @param custom_headers List of custom SIP headers (struct sip_hdr)
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_set_custom_hdrs(struct ua *ua, struct list *custom_headers)
{
	struct le *le;
	int err;

	if (!ua)
		return EINVAL;

	list_flush(&ua->custom_hdrs);

	LIST_FOREACH(custom_headers, le) {
		const struct sip_hdr *hdr = le->data;
		err = ua_add_custom_hdr(ua, &hdr->name, &hdr->val);
		if (err)
			return err;
	}

	return 0;
}


/**
 * Enables SIP auto answer with given method and answer delay in seconds.
 * If SIP auto answer is activated then a SIP header is added to the INVITE
 * request that informs the callee to answer the call after the specified delay
 * automatically. This enables to setup intercom applications.
 * This SIP auto answer headers are supported:
 * - Answer-Mode: Auto (RFC 5373)
 * - Call-Info: <http://www.notused.com>;answer-after=0
 * - Alert-Info: <...>;info=alert-autoanswer;delay=0
 *
 * @param ua      User-Agent
 * @param adelay  Answer delay
 * @param met     SIP auto answer method.
 *
 * @return 0 if success, otherwise errorcode
 */
int  ua_enable_autoanswer(struct ua *ua, int32_t adelay,
		enum answer_method met)
{
	struct pl n;
	struct pl v;
	struct mbuf *mb = NULL;
	struct pl val = PL("<>");
	int err = 0;
	const char *name;

	if (adelay < 0)
		met =  ANSM_NONE;

	if (met) {
		mb = mbuf_alloc(20);
		if (!mb)
			return ENOMEM;
	}

	if (ua->ansval)
		pl_set_str(&val, ua->ansval);

	switch (met) {

	case ANSM_RFC5373:
		err = mbuf_printf(mb, "Auto");
		break;
	case ANSM_CALLINFO:
		err = mbuf_printf(mb, "%r;answer-after=%d", &val, adelay);
		break;
	case ANSM_ALERTINFO:
		err = mbuf_printf(mb, "%r;info=alert-autoanswer;delay=%d",
				&val, adelay);
		break;
	default:
		err = EINVAL;
		break;
	}

	if (err)
		goto out;

	name = autoans_header_name(met);
	pl_set_str(&n, name);
	mbuf_set_pos(mb, 0);
	pl_set_mbuf(&v, mb);
	err = ua_add_custom_hdr(ua, &n, &v);

out:
	mem_deref(mb);
	return err;
}


/**
 * Disables SIP auto answer with given method.
 *
 * @param ua      User-Agent
 * @param met     SIP auto answer method.
 *
 * @return 0 if success, otherwise errorcode
 */
int  ua_disable_autoanswer(struct ua *ua, enum answer_method met)
{
	struct pl n;
	const char *name;

	name = autoans_header_name(met);
	if (!name)
		return EINVAL;

	pl_set_str(&n, name);
	return ua_rm_custom_hdr(ua, &n);
}


int ua_raise(struct ua *ua)
{
	if (!ua)
		return EINVAL;

	return uag_raise(ua, &ua->le);
}


int ua_set_autoanswer_value(struct ua *ua, const char *value)
{
	if (!ua)
		return EINVAL;

	ua->ansval = mem_deref(ua->ansval);
	if (!value)
		return 0;

	return str_dup(&ua->ansval, value);
}


/**
 * Return true if the out-of-dialog SIP request is allowed for this UA
 *
 * @param ua  User-Agent
 * @param msg SIP message
 *
 * @return true if the request is allowed, false otherwise
 */
bool ua_req_allowed(const struct ua *ua, const struct sip_msg *msg)
{
	if (!ua || !msg)
		return false;

	return account_inreq_mode(ua->acc) == INREQ_MODE_ON;
}


/**
 * Return false if filter_registrar is active for the message transport and the
 * SIP request IP does not match the server IP
 *
 * @param ua  User-Agent
 * @param msg SIP message
 *
 * @return true if the request is allowed, false otherwise
 */
bool ua_req_check_origin(const struct ua *ua, const struct sip_msg *msg)
{
	struct le *le;

	if (!ua || !msg)
		return false;

	if (!u32mask_enabled(uag_cfg()->reg_filt, msg->tp))
		return true;

	for (le = ua->regl.head; le; le = le->next) {
		struct reg *reg = le->data;
		if (sa_cmp(reg_paddr(reg), &msg->src, SA_ADDR))
			return true;
	}

	return false;
}
