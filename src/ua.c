/**
 * @file src/ua.c  User-Agent
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"
#include <ctype.h>


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
	int af_media;                /**< Preferred Address Family for media */
	enum presence_status my_status; /**< Presence Status                 */
	bool catchall;               /**< Catch all inbound requests         */
	struct list hdr_filter;      /**< Filter for incoming headers        */
	struct list custom_hdrs;     /**< List of outgoing headers           */
};

struct ua_eh {
	struct le le;
	ua_event_h *h;
	void *arg;
};

struct ua_xhdr_filter {
	struct le le;
	char *hdr_name;
};

static struct {
	struct config_sip *cfg;        /**< SIP configuration               */
	struct list ual;               /**< List of User-Agents (struct ua) */
	struct list ehl;               /**< Event handlers (struct ua_eh)   */
	struct sip *sip;               /**< SIP Stack                       */
	struct sip_lsnr *lsnr;         /**< SIP Listener                    */
	struct sipsess_sock *sock;     /**< SIP Session socket              */
	struct sipevent_sock *evsock;  /**< SIP Event socket                */
	struct ua *ua_cur;             /**< Current User-Agent              */
	bool use_udp;                  /**< Use UDP transport               */
	bool use_tcp;                  /**< Use TCP transport               */
	bool use_tls;                  /**< Use TLS transport               */
	bool delayed_close;            /**< Module will close SIP stack     */
	sip_msg_h *subh;               /**< Subscribe handler               */
	ua_exit_h *exith;              /**< UA Exit handler                 */
	void *arg;                     /**< UA Exit handler argument        */
	char *eprm;                    /**< Extra UA parameters             */
#ifdef USE_TLS
	struct tls *tls;               /**< TLS Context                     */
#endif
} uag = {
	NULL,
	LIST_INIT,
	LIST_INIT,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	true,
	true,
	true,
	false,
	NULL,
	NULL,
	NULL,
	NULL,
#ifdef USE_TLS
	NULL,
#endif
};


static void ua_destructor(void *arg)
{
	struct ua *ua = arg;

	list_unlink(&ua->le);

	if (!list_isempty(&ua->regl))
		ua_event(ua, UA_EVENT_UNREGISTERING, NULL, NULL);

	list_flush(&ua->calls);
	list_flush(&ua->regl);
	mem_deref(ua->cuser);
	mem_deref(ua->pub_gruu);
	mem_deref(ua->acc);

	if (uag.delayed_close && list_isempty(&uag.ual)) {
		sip_close(uag.sip, false);
	}

	list_flush(&ua->custom_hdrs);
	list_flush(&ua->hdr_filter);
}


/* This function is called when all SIP transactions are done */
static void exit_handler(void *arg)
{
	(void)arg;

	ua_event(NULL, UA_EVENT_EXIT, NULL, NULL);

	debug("ua: sip-stack exit\n");

	if (uag.exith)
		uag.exith(uag.arg);
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


/**
 * Send a User-Agent event to all UA event handlers
 *
 * @param ua   User-Agent object (optional)
 * @param ev   User-agent event
 * @param call Call object (optional)
 * @param fmt  Formatted arguments
 * @param ...  Variable arguments
 */
void ua_event(struct ua *ua, enum ua_event ev, struct call *call,
	      const char *fmt, ...)
{
	struct le *le;
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	(void)re_vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	/* send event to all clients */
	le = uag.ehl.head;
	while (le) {
		struct ua_eh *eh = le->data;
		le = le->next;

		eh->h(ua, ev, call, buf, eh->arg);
	}
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
	uri.user = uri.password = pl_null;

	err = re_sdprintf(&reg_uri, "%H", uri_encode, &uri);
	if (err)
		goto out;

	if (uag.cfg && str_isset(uag.cfg->uuid)) {
		if (re_snprintf(params, sizeof(params),
				";+sip.instance=\"<urn:uuid:%s>\"",
				uag.cfg->uuid) < 0) {
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

	ua_event(ua, UA_EVENT_REGISTERING, NULL, NULL);

	for (le = ua->regl.head, i=0; le; le = le->next, i++) {
		struct reg *reg = le->data;

		err = reg_register(reg, reg_uri, params,
				   acc->regint, acc->outboundv[i]);
		if (err) {
			warning("ua: SIP register failed: %m\n", err);

			ua_event(ua, UA_EVENT_REGISTER_FAIL, NULL, "%m", err);
			goto out;
		}
	}

 out:
	mem_deref(reg_uri);

	return err;
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
		ua_event(ua, UA_EVENT_UNREGISTERING, NULL, NULL);

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
	ua_event(ua, UA_EVENT_SHUTDOWN, NULL, NULL);

	/* terminate all calls now */
	list_flush(&ua->calls);

	/* number of remaining references (excluding this one) */
	nrefs = mem_nrefs(ua) - 1;

	mem_deref(ua);

	return nrefs;
}


static struct call *ua_find_call_onhold(const struct ua *ua)
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


static void resume_call(struct ua *ua)
{
	struct call *call;

	call = ua_find_call_onhold(ua);
	if (call) {
		ua_printf(ua, "resuming previous call with '%s'\n",
			  call_peeruri(call));
		call_hold(call, false);
	}
}


static void call_event_handler(struct call *call, enum call_event ev,
			       const char *str, void *arg)
{
	struct ua *ua = arg;
	const char *peeruri;

	MAGIC_CHECK(ua);

	peeruri = call_peeruri(call);

	switch (ev) {

	case CALL_EVENT_INCOMING:

		if (contact_block_access(baresip_contacts(),
					 peeruri)) {

			info("ua: blocked access: \"%s\"\n", peeruri);

			ua_event(ua, UA_EVENT_CALL_CLOSED, call, str);
			mem_deref(call);
			break;
		}

		switch (ua->acc->answermode) {

		case ANSWERMODE_EARLY:
			(void)call_progress(call);
			break;

		case ANSWERMODE_AUTO:
			(void)call_answer(call, 200, VIDMODE_ON);
			break;

		case ANSWERMODE_MANUAL:
			ua_event(ua, UA_EVENT_CALL_INCOMING, call, peeruri);
			break;
		}
		break;

	case CALL_EVENT_RINGING:
		ua_event(ua, UA_EVENT_CALL_RINGING, call, peeruri);
		break;

	case CALL_EVENT_PROGRESS:
		ua_printf(ua, "Call in-progress: %s\n", peeruri);
		ua_event(ua, UA_EVENT_CALL_PROGRESS, call, peeruri);
		break;

	case CALL_EVENT_ESTABLISHED:
		ua_printf(ua, "Call established: %s\n", peeruri);
		ua_event(ua, UA_EVENT_CALL_ESTABLISHED, call, peeruri);
		break;

	case CALL_EVENT_CLOSED:
		ua_event(ua, UA_EVENT_CALL_CLOSED, call, str);
		mem_deref(call);

		resume_call(ua);
		break;

	case CALL_EVENT_TRANSFER:
		ua_event(ua, UA_EVENT_CALL_TRANSFER, call, str);
		break;

	case CALL_EVENT_TRANSFER_FAILED:
		ua_event(ua, UA_EVENT_CALL_TRANSFER_FAILED, call, str);
		break;

	case CALL_EVENT_MENC:
		ua_event(ua, UA_EVENT_CALL_MENC, call, str);
		break;
	}
}


static void call_dtmf_handler(struct call *call, char key, void *arg)
{
	struct ua *ua = arg;
	char key_str[2];

	MAGIC_CHECK(ua);

	if (key != KEYCODE_REL) {

		key_str[0] = key;
		key_str[1] = '\0';

		ua_event(ua, UA_EVENT_CALL_DTMF_START, call, key_str);
	}
	else {
		ua_event(ua, UA_EVENT_CALL_DTMF_END, call, NULL);
	}
}


static int best_effort_af(struct ua *ua, const struct network *net)
{
	struct le *le;
	const int afv[2] = { AF_INET, AF_INET6 };
	size_t i;

	for (le = ua->regl.head, i=0; le; le = le->next, i++) {
		const struct reg *reg = le->data;
		if (reg_isok(reg))
			return reg_af(reg);
	}

	for (i=0; i<ARRAY_SIZE(afv); i++) {
		int af = afv[i];

		if (net_af_enabled(net, af) &&
		    sa_isset(net_laddr_af(net, af), SA_ADDR))
			return af;
	}

	return AF_UNSPEC;
}


static int sdp_af_hint(struct mbuf *mb)
{
	struct pl af;
	int err;

	err = re_regex((char *)mbuf_buf(mb), mbuf_get_left(mb),
		       "IN IP[46]+", &af);
	if (err)
		return AF_UNSPEC;

	switch (af.p[0]) {

	case '4': return AF_INET;
	case '6': return AF_INET6;
	}

	return AF_UNSPEC;
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
	int af;
	int af_sdp;
	int err;

	if (!callp || !ua)
		return EINVAL;

	if (msg && (af_sdp = sdp_af_hint(msg->mb))) {
		info("ua: using AF from sdp offer: af=%s\n",
		     net_af2name(af_sdp));
		af = af_sdp;
	}
	else if (ua->af_media &&
		   sa_isset(net_laddr_af(net, ua->af_media), SA_ADDR)) {
		info("ua: using ua's preferred AF: af=%s\n",
		     net_af2name(ua->af_media));
		af = ua->af_media;
	}
	else {
		af = best_effort_af(ua, net);
		info("ua: using best effort AF: af=%s\n",
		     net_af2name(af));
	}

	memset(&cprm, 0, sizeof(cprm));

	sa_cpy(&cprm.laddr, net_laddr_af(net, af));
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

	call_set_handlers(*callp, NULL, call_dtmf_handler, ua);

	return 0;
}


static void handle_options(struct ua *ua, const struct sip_msg *msg)
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

		err = ua_call_alloc(&call, ua, VIDMODE_ON, NULL, NULL, NULL,
				    false);
		if (err) {
			(void)sip_treply(NULL, uag.sip, msg,
					 500, "Call Error");
			return;
		}

		err = call_sdp_get(call, &desc, true);
		if (err)
			goto out;
	}

	sip_contact_set(&contact, ua_cuser(ua), &msg->dst, msg->tp);

	err = sip_treplyf(NULL, NULL, uag.sip,
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
		warning("ua: options: sip_treplyf: %m\n", err);
	}

 out:
	mem_deref(desc);
	mem_deref(call);
}


static bool request_handler(const struct sip_msg *msg, void *arg)
{
	struct ua *ua;

	(void)arg;

	if (pl_strcmp(&msg->met, "OPTIONS"))
		return false;

	ua = uag_find(&msg->uri.user);
	if (!ua) {
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return true;
	}

	handle_options(ua, msg);

	return true;
}


static void add_extension(struct ua *ua, const char *extension)
{
	struct pl e;

	if (ua->extensionc >= ARRAY_SIZE(ua->extensionv)) {
		warning("ua: maximum %zu number of SIP extensions\n",
			ARRAY_SIZE(ua->extensionv));
		return;
	}

	pl_set_str(&e, extension);

	ua->extensionv[ua->extensionc++] = e;
}


static int create_register_clients(struct ua *ua)
{
	int err = 0;

	/* Register clients */
	if (uag.cfg && str_isset(uag.cfg->uuid))
		add_extension(ua, "gruu");

	if (0 == str_casecmp(ua->acc->sipnat, "outbound")) {

		size_t i;

		add_extension(ua, "path");
		add_extension(ua, "outbound");

		if (!str_isset(uag.cfg->uuid)) {

			warning("ua: outbound requires valid UUID!\n");
			err = ENOSYS;
			goto out;
		}

		for (i=0; i<ARRAY_SIZE(ua->acc->outboundv); i++) {

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

 out:
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
	char *buf = NULL;
	int err;

	if (!aor)
		return EINVAL;

	ua = mem_zalloc(sizeof(*ua), ua_destructor);
	if (!ua)
		return ENOMEM;

	MAGIC_INIT(ua);

	list_init(&ua->calls);

	ua->af_media = AF_UNSPEC;

	/* Decode SIP address */
	if (uag.eprm) {
		err = re_sdprintf(&buf, "%s;%s", aor, uag.eprm);
		if (err)
			goto out;
		aor = buf;
	}

	err = account_alloc(&ua->acc, aor);
	if (err)
		goto out;

	/* generate a unique contact-user, this is needed to route
	   incoming requests when using multiple useragents */
	err = re_sdprintf(&ua->cuser, "%r-%p", &ua->acc->luri.user, ua);
	if (err)
		goto out;

	if (ua->acc->sipnat) {
		ua_printf(ua, "Using sipnat: '%s'\n", ua->acc->sipnat);
	}

	if (ua->acc->mnat) {
		ua_printf(ua, "Using medianat '%s'\n",
			  ua->acc->mnat->id);

		if (0 == str_casecmp(ua->acc->mnat->id, "ice"))
			add_extension(ua, "ice");
	}

	if (ua->acc->menc) {
		ua_printf(ua, "Using media encryption '%s'\n",
			  ua->acc->menc->id);
	}

	err = create_register_clients(ua);
	if (err)
		goto out;

	list_append(&uag.ual, &ua->le, ua);

	if (!uag_current())
		uag_current_set(ua);

 out:
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
 * Auto complete a SIP uri, add scheme and domain if missing
 *
 * @param ua  User-Agent
 * @param buf Target buffer to print SIP uri
 * @param uri Input SIP uri
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_uri_complete(struct ua *ua, struct mbuf *buf, const char *uri)
{
	struct account *acc;
	struct sa sa_addr;
	size_t len;
	bool uri_is_ip;
	int err = 0;

	if (!ua || !buf || !uri)
		return EINVAL;

	acc = ua->acc;

	/* Skip initial whitespace */
	while (isspace(*uri))
		++uri;

	len = str_len(uri);

	/* Append sip: scheme if missing */
	if (0 != re_regex(uri, len, "sip:"))
		err |= mbuf_printf(buf, "sip:");

	err |= mbuf_write_str(buf, uri);

	/* Append domain if missing and uri is not IP address */

	/* check if uri is valid IP address */
	uri_is_ip = (0 == sa_set_str(&sa_addr, uri, 0));

	if (0 != re_regex(uri, len, "[^@]+@[^]+", NULL, NULL) &&
		1 != uri_is_ip) {
#if HAVE_INET6
		if (AF_INET6 == acc->luri.af)
			err |= mbuf_printf(buf, "@[%r]",
					   &acc->luri.host);
		else
#endif
			err |= mbuf_printf(buf, "@%r",
					   &acc->luri.host);

		/* Also append port if specified and not 5060 */
		switch (acc->luri.port) {

		case 0:
		case SIP_PORT:
			break;

		default:
			err |= mbuf_printf(buf, ":%u", acc->luri.port);
			break;
		}
	}

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
	struct call *call = NULL;
	struct mbuf *dialbuf;
	struct pl pl;
	int err = 0;

	if (!ua || !str_isset(req_uri))
		return EINVAL;

	dialbuf = mbuf_alloc(64);
	if (!dialbuf)
		return ENOMEM;

	err |= ua_uri_complete(ua, dialbuf, req_uri);

	/* Append any optional URI parameters */
	err |= mbuf_write_pl(dialbuf, &ua->acc->luri.params);

	if (err)
		goto out;

	err = ua_call_alloc(&call, ua, vmode, NULL, NULL, from_uri, true);
	if (err)
		goto out;

	pl.p = (char *)dialbuf->buf;
	pl.l = dialbuf->end;

	if (!list_isempty(&ua->custom_hdrs))
		call_set_custom_hdrs(call, &ua->custom_hdrs);

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
	if (!ua)
		return;

	if (!call) {
		call = ua_call(ua);
		if (!call)
			return;
	}

	call_hangup(call, scode, reason);

	ua_event(ua, UA_EVENT_CALL_CLOSED, call,
		 reason ? reason : "Connection reset by user");

	mem_deref(call);

	resume_call(ua);
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
		call = ua_call(ua);
		if (!call)
			return ENOENT;
	}

	return call_answer(call, 200, vmode);
}


/**
 * Put the current call on hold and answer the incoming call
 *
 * @param ua    User-Agent
 * @param call  Call to answer, or NULL for current call
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
		call = ua_call(ua);
		if (!call)
			return ENOENT;
	}

	/* put previous call on-hold */
	pcall = ua_prev_call(ua);
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
	struct mbuf *dialbuf;
	int err = 0;

	if (!ua || !str_isset(uri))
		return EINVAL;

	dialbuf = mbuf_alloc(64);
	if (!dialbuf)
		return ENOMEM;

	err = ua_uri_complete(ua, dialbuf, uri);
	if (err)
		goto out;

	dialbuf->buf[dialbuf->end] = '\0';

	err = sip_req_send(ua, "OPTIONS", (char *)dialbuf->buf, resph, arg,
			   "Accept: application/sdp\r\n"
			   "Content-Length: 0\r\n"
			   "\r\n");
	if (err) {
		warning("ua: send options: (%m)\n", err);
	}

 out:
	mem_deref(dialbuf);

	return err;
}


/**
 * Get the AOR of a User-Agent
 *
 * @param ua User-Agent object
 *
 * @return AOR
 */
const char *ua_aor(const struct ua *ua)
{
	return ua ? account_aor(ua->acc) : NULL;
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
	return ua ? ua->my_status : PRESENCE_UNKNOWN;
}


/**
 * Set presence status of a User-Agent
 *
 * @param ua     User-Agent object
 * @param status Presence status
 */
void ua_presence_status_set(struct ua *ua, const enum presence_status status)
{
	if (!ua)
		return;

	ua->my_status = status;
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
 * Get the previous call
 *
 * @param ua User-Agent
 *
 * @return Previous call or NULL if none
 */
struct call *ua_prev_call(const struct ua *ua)
{
	struct le *le;
	int prev = 0;

	if (!ua)
		return NULL;

	for (le = ua->calls.tail; le; le = le->prev) {
		if ( prev == 1) {
			struct call *call = le->data;
			return call;
		}
		if ( prev == 0)
			prev = 1;
	}

	return NULL;
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
	err |= re_hprintf(pf, " af_media:  %s\n", net_af2name(ua->af_media));
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
	err |= odict_entry_add(od, "selected_ua", ODICT_BOOL,
			ua == uag_current());

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


/* One instance */


static int add_transp_af(const struct sa *laddr)
{
	struct sa local;
	int err = 0;

	if (str_isset(uag.cfg->local)) {
		err = sa_decode(&local, uag.cfg->local,
				str_len(uag.cfg->local));
		if (err) {
			err = sa_set_str(&local, uag.cfg->local, 0);
			if (err) {
				warning("ua: decode failed: '%s'\n",
					uag.cfg->local);
				return err;
			}
		}

		if (!sa_isset(&local, SA_ADDR)) {
			uint16_t port = sa_port(&local);
			(void)sa_set_sa(&local, &laddr->u.sa);
			sa_set_port(&local, port);
		}

		if (sa_af(laddr) != sa_af(&local))
			return 0;
	}
	else {
		sa_cpy(&local, laddr);
		sa_set_port(&local, 0);
	}

	if (uag.use_udp)
		err |= sip_transp_add(uag.sip, SIP_TRANSP_UDP, &local);
	if (uag.use_tcp)
		err |= sip_transp_add(uag.sip, SIP_TRANSP_TCP, &local);
	if (err) {
		warning("ua: SIP Transport failed: %m\n", err);
		return err;
	}

#ifdef USE_TLS
	if (uag.use_tls) {
		/* Build our SSL context*/
		if (!uag.tls) {
			const char *cert = NULL;

			if (str_isset(uag.cfg->cert)) {
				cert = uag.cfg->cert;
				info("SIP Certificate: %s\n", cert);
			}

			err = tls_alloc(&uag.tls, TLS_METHOD_SSLV23,
					cert, NULL);
			if (err) {
				warning("ua: tls_alloc() failed: %m\n", err);
				return err;
			}

			if (str_isset(uag.cfg->cafile)) {
				const char *ca = uag.cfg->cafile;

				info("ua: adding SIP CA: %s\n", ca);

				err = tls_add_ca(uag.tls, ca);
				if (err) {
					warning("ua: tls_add_ca() failed:"
						" %m\n", err);
					return err;
				}
			}
		}

		if (sa_isset(&local, SA_PORT))
			sa_set_port(&local, sa_port(&local) + 1);

		err = sip_transp_add(uag.sip, SIP_TRANSP_TLS, &local, uag.tls);
		if (err) {
			warning("ua: SIP/TLS transport failed: %m\n", err);
			return err;
		}
	}
#endif

	return err;
}


static int ua_add_transp(struct network *net)
{
	int err = 0;

	if (sa_isset(net_laddr_af(net, AF_INET), SA_ADDR))
		err |= add_transp_af(net_laddr_af(net, AF_INET));

#if HAVE_INET6
	if (sa_isset(net_laddr_af(net, AF_INET6), SA_ADDR))
		err |= add_transp_af(net_laddr_af(net, AF_INET6));
#endif

	return err;
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


/* Handle incoming calls */
static void sipsess_conn_handler(const struct sip_msg *msg, void *arg)
{
	struct config *config = conf_config();
	const struct network *net = baresip_network();
	const struct sip_hdr *hdr;
	int af_sdp;
	struct ua *ua;
	struct call *call = NULL;
	char to_uri[256];
	int err;

	(void)arg;

	debug("ua: sipsess connect via %s %J --> %J\n",
	      sip_transp_name(msg->tp),
	      &msg->src, &msg->dst);

	ua = uag_find(&msg->uri.user);
	if (!ua) {
		warning("ua: %r: UA not found: %r\n",
			&msg->from.auri, &msg->uri.user);
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return;
	}

	/* handle multiple calls */
	if (config->call.max_calls &&
	    list_count(&ua->calls) + 1 > config->call.max_calls) {

		info("ua: rejected call from %r (maximum %d calls)\n",
		     &msg->from.auri, config->call.max_calls);
		(void)sip_treply(NULL, uag.sip, msg, 486, "Max Calls");
		return;
	}

	/* Handle Require: header, check for any required extensions */
	hdr = sip_msg_hdr_apply(msg, true, SIP_HDR_REQUIRE,
				require_handler, ua);
	if (hdr) {
		info("ua: call from %r rejected with 420"
			     " -- option-tag '%r' not supported\n",
			     &msg->from.auri, &hdr->val);

		(void)sip_treplyf(NULL, NULL, uag.sip, msg, false,
				  420, "Bad Extension",
				  "Unsupported: %r\r\n"
				  "Content-Length: 0\r\n\r\n",
				  &hdr->val);
		return;
	}

	/* Check if offered media AF is supported and available */
	af_sdp = sdp_af_hint(msg->mb);
	if (af_sdp) {
		if (!net_af_enabled(net, af_sdp)) {
			warning("ua: SDP offer AF not supported (%s)\n",
				net_af2name(af_sdp));
			af_sdp = 0;
		}
		else if (!sa_isset(net_laddr_af(net, af_sdp), SA_ADDR)) {
			warning("ua: SDP offer AF not available (%s)\n",
				net_af2name(af_sdp));
			af_sdp = 0;
		}
		if (!af_sdp) {
			(void)sip_treply(NULL, uag_sip(), msg, 488,
					 "Not Acceptable Here");
			return;
		}
	}

	(void)pl_strcpy(&msg->to.auri, to_uri, sizeof(to_uri));

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

	err = call_accept(call, uag.sock, msg);
	if (err)
		goto error;

	return;

 error:
	mem_deref(call);
	(void)sip_treply(NULL, uag.sip, msg, 500, "Call Error");
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


static bool sub_handler(const struct sip_msg *msg, void *arg)
{
	struct ua *ua;

	(void)arg;

	ua = uag_find(&msg->uri.user);
	if (!ua) {
		warning("subscribe: no UA found for %r\n", &msg->uri.user);
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return true;
	}

	if (uag.subh)
		uag.subh(msg, ua);

	return true;
}


#ifdef LIBRE_HAVE_SIPTRACE
static void sip_trace_handler(bool tx, enum sip_transp tp,
			      const struct sa *src, const struct sa *dst,
			      const uint8_t *pkt, size_t len, void *arg)
{
	(void)tx;
	(void)arg;

	re_printf("\x1b[36;1m"
		  "#\n"
		  "%s %J -> %J\n"
		  "%b"
		  "\x1b[;m\n"
		  ,
		  sip_transp_name(tp), src, dst, pkt, len);
}
#endif


/**
 * Initialise the User-Agents
 *
 * @param software    SIP User-Agent string
 * @param udp         Enable UDP transport
 * @param tcp         Enable TCP transport
 * @param tls         Enable TLS transport
 *
 * @return 0 if success, otherwise errorcode
 */
int ua_init(const char *software, bool udp, bool tcp, bool tls)
{
	struct config *cfg = conf_config();
	struct network *net = baresip_network();
	uint32_t bsize;
	int err;

	if (!net) {
		warning("ua: no network\n");
		return EINVAL;
	}

	uag.cfg = &cfg->sip;
	bsize = 16;

	uag.use_udp = udp;
	uag.use_tcp = tcp;
	uag.use_tls = tls;

	list_init(&uag.ual);

	err = sip_alloc(&uag.sip, net_dnsc(net), bsize, bsize, bsize,
			software, exit_handler, NULL);
	if (err) {
		warning("ua: sip stack failed: %m\n", err);
		goto out;
	}

	err = ua_add_transp(net);
	if (err)
		goto out;

	err = sip_listen(&uag.lsnr, uag.sip, true, request_handler, NULL);
	if (err)
		goto out;

	err = sipsess_listen(&uag.sock, uag.sip, bsize,
			     sipsess_conn_handler, NULL);
	if (err)
		goto out;

	err = sipevent_listen(&uag.evsock, uag.sip, bsize, bsize,
			      sub_handler, NULL);
	if (err)
		goto out;

 out:
	if (err) {
		warning("ua: init failed (%m)\n", err);
		ua_close();
	}
	return err;
}


/**
 * Close all active User-Agents
 */
void ua_close(void)
{
	uag.evsock   = mem_deref(uag.evsock);
	uag.sock     = mem_deref(uag.sock);
	uag.lsnr     = mem_deref(uag.lsnr);
	uag.sip      = mem_deref(uag.sip);
	uag.eprm     = mem_deref(uag.eprm);

#ifdef USE_TLS
	uag.tls = mem_deref(uag.tls);
#endif

	list_flush(&uag.ual);
	list_flush(&uag.ehl);
}


/**
 * Stop all User-Agents
 *
 * @param forced True to force, otherwise false
 */
void ua_stop_all(bool forced)
{
	struct le *le;
	unsigned ext_ref = 0;

	info("ua: stop all (forced=%d)\n", forced);

	/* check if someone else has grabbed a ref to ua */
	le = uag.ual.head;
	while (le) {

		struct ua *ua = le->data;
		le = le->next;

		if (ua_destroy(ua) != 0) {
			++ext_ref;
		}
	}

	if (ext_ref) {
		info("ua: in use (%u) by app module\n", ext_ref);
		uag.delayed_close = true;
		return;
	}

	if (forced)
		sipsess_close_all(uag.sock);

	sip_close(uag.sip, forced);
}


/**
 * Set the global UA exit handler. The exit handler will be called
 * asyncronously when the SIP stack has exited.
 *
 * @param exith Exit handler
 * @param arg   Handler argument
 */
void uag_set_exit_handler(ua_exit_h *exith, void *arg)
{
	uag.exith = exith;
	uag.arg = arg;
}


/**
 * Enable SIP message tracing
 *
 * @param enable True to enable, false to disable
 */
void uag_enable_sip_trace(bool enable)
{
#ifdef LIBRE_HAVE_SIPTRACE
	sip_set_trace_handler(uag.sip, enable ? sip_trace_handler : NULL);
#else
	(void)enable;
	warning("no sip trace in libre\n");
#endif
}


/**
 * Reset the SIP transports for all User-Agents
 *
 * @param reg      True to reset registration
 * @param reinvite True to update active calls
 *
 * @return 0 if success, otherwise errorcode
 */
int uag_reset_transp(bool reg, bool reinvite)
{
	struct network *net = baresip_network();
	struct le *le;
	int err;

	/* Update SIP transports */
	sip_transp_flush(uag.sip);

	(void)net_check(net);
	err = ua_add_transp(net);
	if (err)
		return err;

	/* Re-REGISTER all User-Agents */
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		if (reg && ua->acc->regint) {
			err |= ua_register(ua);
		}

		/* update all active calls */
		if (reinvite) {
			struct le *lec;

			for (lec = ua->calls.head; lec; lec = lec->next) {
				struct call *call = lec->data;
				const struct sa *laddr;

				laddr = net_laddr_af(net, call_af(call));

				err |= call_reset_transp(call, laddr);
			}
		}
	}

	return err;
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

	err |= re_hprintf(pf, "\n--- Active calls (%u) ---\n",
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
 * Get the global SIP Stack
 *
 * @return SIP Stack
 */
struct sip *uag_sip(void)
{
	return uag.sip;
}


/**
 * Get the global SIP Session socket
 *
 * @return SIP Session socket
 */
struct sipsess_sock *uag_sipsess_sock(void)
{
	return uag.sock;
}


/**
 * Get the global SIP Event socket
 *
 * @return SIP Event socket
 */
struct sipevent_sock *uag_sipevent_sock(void)
{
	return uag.evsock;
}


/**
 * Find the correct UA from the contact user
 *
 * @param cuser Contact username
 *
 * @return Matching UA if found, NULL if not found
 */
struct ua *uag_find(const struct pl *cuser)
{
	struct le *le;

	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		if (0 == pl_strcasecmp(cuser, ua->cuser))
			return ua;
	}

	/* Try also matching by AOR, for better interop */
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		if (0 == pl_casecmp(cuser, &ua->acc->luri.user))
			return ua;
	}

	/* Last resort, try any catchall UAs */
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		if (ua->catchall)
			return ua;
	}

	return NULL;
}


/**
 * Find a User-Agent (UA) from an Address-of-Record (AOR)
 *
 * @param aor Address-of-Record string
 *
 * @return User-Agent (UA) if found, otherwise NULL
 */
struct ua *uag_find_aor(const char *aor)
{
	struct le *le;

	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		if (str_isset(aor) && str_cmp(ua->acc->aor, aor))
			continue;

		return ua;
	}

	return NULL;
}


/**
 * Find a User-Agent (UA) which has certain address parameter and/or value
 *
 * @param name  SIP Address parameter name
 * @param value SIP Address parameter value (optional)
 *
 * @return User-Agent (UA) if found, otherwise NULL
 */
struct ua *uag_find_param(const char *name, const char *value)
{
	struct le *le;

	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;
		struct sip_addr *laddr = account_laddr(ua->acc);
		struct pl val;

		if (value) {

			if (0 == msg_param_decode(&laddr->params, name, &val)
			    &&
			    0 == pl_strcasecmp(&val, value)) {
				return ua;
			}
		}
		else {
			if (0 == msg_param_exists(&laddr->params, name, &val))
				return ua;
		}
	}

	return NULL;
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
 * Get the list of User-Agents
 *
 * @return List of User-Agents (struct ua)
 */
struct list *uag_list(void)
{
	return &uag.ual;
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
			 "NOTIFY,SUBSCRIBE,INFO,MESSAGE");

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


static void eh_destructor(void *arg)
{
	struct ua_eh *eh = arg;
	list_unlink(&eh->le);
}


/**
 * Register a User-Agent event handler
 *
 * @param h   Event handler
 * @param arg Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int uag_event_register(ua_event_h *h, void *arg)
{
	struct ua_eh *eh;

	if (!h)
		return EINVAL;

	uag_event_unregister(h);

	eh = mem_zalloc(sizeof(*eh), eh_destructor);
	if (!eh)
		return ENOMEM;

	eh->h = h;
	eh->arg = arg;

	list_append(&uag.ehl, &eh->le, eh);

	return 0;
}


/**
 * Unregister a User-Agent event handler
 *
 * @param h   Event handler
 */
void uag_event_unregister(ua_event_h *h)
{
	struct le *le;

	for (le = uag.ehl.head; le; le = le->next) {

		struct ua_eh *eh = le->data;

		if (eh->h == h) {
			mem_deref(eh);
			break;
		}
	}
}


/**
 * Set the handler to receive incoming SIP SUBSCRIBE messages
 *
 * @param subh Subscribe handler
 */
void uag_set_sub_handler(sip_msg_h *subh)
{
	uag.subh = subh;
}


/**
 * Set the current User-Agent
 *
 * @param ua User-Agent to set as current
 */
void uag_current_set(struct ua *ua)
{
	uag.ua_cur = ua;
}


/**
 * Get the current User-Agent
 *
 * @return Current User-Agent, NULL if none
 */
struct ua *uag_current(void)
{
	if (list_isempty(uag_list()))
		return NULL;

	return uag.ua_cur;
}


/**
 * Set the preferred address family for media
 *
 * @param ua       User-Agent
 * @param af_media Address family (e.g. AF_INET, AF_INET6)
 */
void ua_set_media_af(struct ua *ua, int af_media)
{
	if (!ua)
		return;

	ua->af_media = af_media;
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

	ua->catchall = enabled;
}


/**
 * Set extra parameters to use for all SIP Accounts
 *
 * @param eprm Extra parameters
 *
 * @return 0 if success, otherwise errorcode
 */
int uag_set_extra_params(const char *eprm)
{
	uag.eprm = mem_deref(uag.eprm);

	if (eprm)
		return str_dup(&uag.eprm, eprm);

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
		struct sip_hdr *hdr = le->data;
		char *buf;

		err = pl_strdup(&buf, &hdr->name);
		if (err)
			return err;

		err = custom_hdrs_add(&ua->custom_hdrs, buf, "%r", &hdr->val);
		mem_deref(buf);
		if (err)
			return err;
	}

	return 0;
}
