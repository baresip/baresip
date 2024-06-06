/**
 * @file src/uag.c  User-Agent Group
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "core.h"

/* One instance */

static struct uag uag = {.ual = LIST_INIT};


/* This function is called when all SIP transactions are done */
static void exit_handler(void *arg)
{
	(void)arg;

	ua_event(NULL, UA_EVENT_EXIT, NULL, NULL);

	debug("ua: sip-stack exit\n");

	if (uag.exith)
		uag.exith(uag.arg);
}


/**
 * Resume the given call and put the established call on hold. If there is no
 * call on hold, then this function does nothing
 *
 * @param call  Call to resume, or NULL to choose one which is on-hold
 *
 * @return 0 if success, otherwise errorcode
 */
int uag_hold_resume(struct call *call)
{
	int err = 0;
	struct le *le = NULL;
	struct ua *ua = NULL;
	struct call *acall = NULL, *toresume = NULL;

	if (call && call_is_onhold(call))
		toresume = call;

	for (le = list_head(&uag.ual); le && !toresume; le = le->next) {
		ua = le->data;
		toresume = ua_find_call_onhold(ua);
	}

	if (!toresume) {
		debug ("ua: no call to resume\n");
		return 0;
	}

	for (le = list_head(&uag.ual); le && !acall; le = le->next) {
		ua = le->data;
		acall = ua_find_active_call(ua);
	}

	if (acall && toresume)
		err =  call_hold(acall, true);

	if (toresume)
		err |= call_hold(toresume, false);

	return err;
}


/**
 * Put all established calls on hold, except the given one
 *
 * @param call  Excluded call, or NULL
 *
 * @return 0 if success, otherwise errorcode
 */
int uag_hold_others(struct call *call)
{
	int err = 0;
	struct le *le = NULL;
	struct call *acall = NULL;

	if (!conf_config()->call.hold_other_calls) {
		return 0;
	}

	for (le = list_head(&uag.ual); le && !acall; le = le->next) {
		struct ua *ua = le->data;
		struct le *lec = NULL;

		for (lec = list_head(ua_calls(ua)); lec; lec = lec->next) {
			struct call *ccall = lec->data;
			if (ccall == call)
				continue;

			if (call_state(ccall) == CALL_STATE_ESTABLISHED &&
					!call_is_onhold(ccall)) {
				acall = ccall;
				break;
			}
		}
	}

	if (!acall || call_state(acall) == CALL_STATE_TRANSFER)
		return 0;

	err = call_hold(acall, true);
	return err;
}


/**
 * Find call with given id
 *
 * @param id  Call-id string
 *
 * @return The call if found, otherwise NULL.
 */
struct call *uag_call_find(const char *id)
{
	struct le *le = NULL;
	struct ua *ua = NULL;
	struct call *call = NULL;

	if (!str_isset(id))
		return NULL;

	for (le = list_head(&uag.ual); le; le = le->next) {
		ua = le->data;

		call = call_find_id(ua_calls(ua), id);
		if (call)
			break;
	}

	return call;
}


/**
 * Filters the calls of all User-Agents
 *
 * @param listh   Call list handler is called for each match
 * @param matchh  Optional filter match handler (if NULL all calls are listed)
 * @param arg     User argument passed to listh
 */
void uag_filter_calls(call_list_h *listh, call_match_h *matchh, void *arg)
{
	struct le *leu;

	if (!listh)
		return;

	for (leu = list_head(uag_list()); leu; leu = leu->next) {
		struct ua *ua = leu->data;
		struct le *lec;

		for (lec = list_tail(ua_calls(ua)); lec; lec = lec->prev) {
			struct call *call = lec->data;

			if (!matchh || matchh(call, arg))
				listh(call, arg);
		}
	}
}


static bool request_handler(const struct sip_msg *msg, void *arg)
{
	struct ua *ua;

	(void)arg;

	if (pl_strcmp(&msg->met, "OPTIONS") &&
	    pl_strcmp(&msg->met, "REFER"))
		return false;

	ua = uag_find_msg(msg);
	if (!ua) {
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return true;
	}

	if (!pl_strcmp(&msg->met, "OPTIONS")) {
		ua_handle_options(ua, msg);
		return true;
	}

	if (!pl_strcmp(&msg->met, "REFER") && !pl_isset(&msg->to.tag))
		return ua_handle_refer(ua, msg);

	return false;
}


static bool uri_only_user(const struct uri *uri)
{
	bool ret;
	struct sa sa;

	/* Note:
	 * If only user is given then uri_decode sets uri->host instead of
	 * uri->user. We don't know if this is a bug. But if somebody changes
	 * this behavior then the following line has to be adapted.          */
	ret = pl_isset(&uri->host) && !pl_isset(&uri->user);

	/* exclude IP addresses */
	if (!sa_set(&sa, &uri->host, 0))
		ret = false;

	return ret;
}


static bool uri_user_and_host(const struct uri *uri)
{
	bool ret;

	ret = pl_isset(&uri->host) && pl_isset(&uri->user);

	return ret;
}


#ifdef USE_TLS
static int add_account_certs(void)
{
	struct le *le;
	char *host;
	int err = 0;

	for (le = list_head(&uag.ual); le; le = le->next) {
		struct account *acc = ua_account(le->data);
		struct uri *luri;
		if (acc->cert) {
			err = sip_transp_add_ccert(uag.sip,
					&acc->laddr.uri, acc->cert);
			if (err) {
				warning("uag: SIP/TLS add client "
					"certificate %s failed: %m\n",
					acc->cert, err);
				return err;
			}

			host = NULL;
			luri = account_luri(acc);
			if (luri) {
				err = pl_strdup(&host, &luri->host);
				if (err)
					return err;
			}

			err = tls_add_certf(uag.tls, acc->cert, host);
			mem_deref(host);
			if (err) {
				warning("uag: SIP/TLS add server "
					"certificate %s failed: %m\n",
					acc->cert, err);
				return err;
			}
		}
	}

	return err;
}
#endif


static int uag_transp_add(const struct sa *laddr)
{
	struct sa local;
#ifdef USE_TLS
	const char *cert = NULL;
	const char *cafile = NULL;
	const char *capath = NULL;
#endif
	int err = 0;

	if (!sa_isset(laddr, SA_ADDR))
		return EINVAL;

	debug("uag: add local address %j\n", laddr);
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

		if (!sa_cmp(laddr, &local, SA_ADDR))
			return 0;
	}
	else {
		sa_cpy(&local, laddr);
		sa_set_port(&local, 0);
	}

	if (u32mask_enabled(uag.transports, SIP_TRANSP_UDP))
		err |= sip_transp_add(uag.sip, SIP_TRANSP_UDP, &local);
	if (u32mask_enabled(uag.transports, SIP_TRANSP_TCP))
		err |= sip_transp_add(uag.sip, SIP_TRANSP_TCP, &local);
	if (err) {
		warning("ua: SIP Transport failed: %m\n", err);
		return err;
	}

#ifdef USE_TLS
	if (u32mask_enabled(uag.transports, SIP_TRANSP_TLS)) {
		/* Build our SSL context*/
		if (!uag.tls) {
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

			if (str_isset(uag.cfg->cafile))
				cafile = uag.cfg->cafile;
			if (str_isset(uag.cfg->capath))
				capath = uag.cfg->capath;

			if (cafile || capath) {
				info("ua: adding SIP CA file: %s\n", cafile);
				info("ua: adding SIP CA path: %s\n", capath);

				err = tls_add_cafile_path(uag.tls,
					cafile, capath);
				if (err) {
					warning("ua: tls_add_ca() failed:"
						" %m\n", err);
				}
			}

			if (!uag.cfg->verify_server)
				tls_disable_verify_server(uag.tls);

			if (uag.cfg->verify_client)
				tls_enable_verify_client(uag.tls, true);

			tls_set_resumption(uag.tls, uag.cfg->tls_resume);
		}

		if (sa_isset(&local, SA_PORT))
			sa_set_port(&local, sa_port(&local) + 1);

		err = sip_transp_add(uag.sip, SIP_TRANSP_TLS, &local, uag.tls);
		if (err) {
			warning("ua: SIP/TLS transport failed: %m\n", err);
			return err;
		}

		err = add_account_certs();
		if (err)
			return err;
	}
#endif

	if (u32mask_enabled(uag.transports, SIP_TRANSP_WS)) {
		err = sip_transp_add_websock(uag.sip, SIP_TRANSP_WS, &local,
				false, NULL, NULL);
		if (err) {
			warning("ua: could not add Websock transport (%m)\n",
					err);
			return err;
		}
	}

#ifdef USE_TLS
	if (u32mask_enabled(uag.transports, SIP_TRANSP_WSS)) {
		if (!uag.wss_tls) {
			err = tls_alloc(&uag.wss_tls, TLS_METHOD_SSLV23,
					NULL, NULL);
			if (err) {
				warning("ua: wss tls_alloc() failed: %m\n",
					err);
				return err;
			}

			err = tls_set_verify_purpose(uag.wss_tls, "sslserver");
			if (err) {
				warning("ua: wss tls_set_verify_purpose() "
					"failed: %m\n", err);
				return err;
			}

			if (cafile || capath) {
				err = tls_add_cafile_path(uag.wss_tls, cafile,
							  capath);
				if (err) {
					warning("ua: wss tls_add_ca() failed:"
							" %m\n", err);
				}
			}

			if (!uag.cfg->verify_server)
				tls_disable_verify_server(uag.wss_tls);
		}
		err = sip_transp_add_websock(uag.sip, SIP_TRANSP_WSS, &local,
				false, uag.cfg->cert, uag.wss_tls);
		if (err) {
			warning("ua: could not add secure Websock transport "
				"(%m)\n", err);
			return err;
		}
	}
#endif

	sip_settos(uag.sip, uag.cfg->tos);
	return err;
}


static bool transp_add_laddr(const char *ifname, const struct sa *sa,
			     void *arg)
{
	int err;
	int *errp = arg;
	(void) ifname;

	err = uag_transp_add(sa);
	if (err) {
		if (errp)
			*errp = err;

		return true;
	}

	return false;
}


static int ua_transp_addall(struct network *net)
{
	int err = 0;
	struct config_sip *cfg = &conf_config()->sip;

	net_laddr_apply(net, transp_add_laddr, &err);
	sip_transp_set_default(uag.sip, cfg->transp);
	return err;
}


static bool sub_handler(const struct sip_msg *msg, void *arg)
{
	struct ua *ua;

	(void)arg;

	ua = uag_find_msg(msg);
	if (!ua) {
		warning("subscribe: no UA found for %r\n", &msg->uri.user);
		(void)sip_treply(NULL, uag_sip(), msg, 404, "Not Found");
		return true;
	}

	if (uag.subh)
		uag.subh(msg, ua);
	else
		(void)sip_treplyf(NULL, NULL, uag_sip(), msg, false, 405,
				 "Method Not Allowed",
				 "Allow: %H\r\n"
				 "Content-Length: 0\r\n\r\n",
				 ua_print_allowed, ua);

	return true;
}


static void sip_trace_handler(bool tx, enum sip_transp tp,
			      const struct sa *src, const struct sa *dst,
			      const uint8_t *pkt, size_t len, void *arg)
{
	(void)tx;
	(void)arg;

	re_printf("\x1b[36;1m"
		  "%H#\n"
		  "%s %J -> %J\n"
		  "%b"
		  "\x1b[;m\n"
		  ,
		  fmt_timestamp, NULL,
		  sip_transp_name(tp), src, dst, pkt, len);
}


/**
 * Initialise the User-Agent Group
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

	if (cfg->sip.transports) {
		uag.transports = cfg->sip.transports;
	}
	else {
		u32mask_enable(&uag.transports, SIP_TRANSP_UDP, udp);
		u32mask_enable(&uag.transports, SIP_TRANSP_TCP, tcp);
		u32mask_enable(&uag.transports, SIP_TRANSP_TLS, tls);
		u32mask_enable(&uag.transports, SIP_TRANSP_WS,  true);
		u32mask_enable(&uag.transports, SIP_TRANSP_WSS, true);
	}

	list_init(&uag.ual);

	err = sip_alloc(&uag.sip, net_dnsc(net), bsize, bsize, bsize,
			software, exit_handler, NULL);
	if (err) {
		warning("ua: sip stack failed: %m\n", err);
		goto out;
	}

	err = ua_transp_addall(net);
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
	uag.wss_tls = mem_deref(uag.wss_tls);
#endif

	list_flush(&uag.ual);
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
	sip_set_trace_handler(uag.sip, enable ? sip_trace_handler : NULL);
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

	err = ua_transp_addall(net);
	if (err)
		return err;

	/* Re-REGISTER all User-Agents */
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);
		struct le *lec;

		if (reg && account_regint(acc) && !account_prio(acc)) {
			err |= ua_register(ua);
		}
		else if (reg && account_regint(acc)) {
			err |= ua_fallback(ua);
		}

		/* update all active calls */
		if (!reinvite)
			continue;

		lec = ua_calls(ua)->head;
		while (lec) {
			struct call *call = lec->data;
			struct stream *s;
			const struct sa *raddr;
			struct sa laddr;
			lec = lec->next;

			s = audio_strm(call_audio(call));
			if (!s)
				s = video_strm(call_video(call));

			if (!s)
				continue;

			raddr = sdp_media_raddr(stream_sdpmedia(s));
			if (net_dst_source_addr_get(raddr, &laddr))
				continue;

			if (sa_cmp(&laddr, call_laddr(call), SA_ADDR))
				continue;

			if (sa_isset(&laddr, SA_ADDR)) {
				if (!call_refresh_allowed(call)) {
					call_hangup(call, 500, "Transport of "
						    "User Agent changed");
					ua_event(ua, UA_EVENT_CALL_CLOSED,
						 call, "Transport of "
						 "User Agent changed");
					mem_deref(call);
					continue;
				}
				err = call_reset_transp(call, &laddr);
			}
		}
	}

	return err;
}


/**
 * Get the global SIP configuration
 *
 * @return SIP Stack
 */
struct config_sip *uag_cfg(void)
{
	return uag.cfg;
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


static bool uri_match_transport(const struct uri *accu,
		const struct uri *peeru, enum sip_transp tp)
{
	struct pl pl;
	enum sip_transp tpa;
	int err;

	err = msg_param_decode(&accu->params, "transport", &pl);
	if (err)
		return true;

	tpa = sip_transp_decode(&pl);
	if (peeru) {
		/* outgoing calls */
		tp = uag.cfg->transp;
		if (!msg_param_decode(&peeru->params, "transport", &pl))
			tp = sip_transp_decode(&pl);
	}

	return tpa == tp;
}


static bool uri_match_af(const struct uri *accu, const struct uri *peeru)
{
	struct sa sa1;
	struct sa sa2;
	int err;

	/* we list cases where we know there is a mismatch in af */
	if (peeru->af == AF_UNSPEC || accu->af == AF_UNSPEC)
		return true;

	if (accu->af != peeru->af)
		return false;

	if (accu->af == AF_INET6 && peeru->af == AF_INET6) {
		err =  sa_set(&sa1, &accu->host, 0);
		err |= sa_set(&sa2, &peeru->host, 0);

		if (err) {
			warning("ua: No valid IPv6 URI %r, %r (%m)\n",
					&accu->host,
					&peeru->host, err);
			return false;
		}

		return sa_is_linklocal(&sa1) == sa_is_linklocal(&sa2);
	}

	/* both IPv4 or we can't decide if af will match */
	return true;
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

		if (0 == pl_strcasecmp(cuser, ua_local_cuser(ua)))
			return ua;
	}

	/* Try also matching by AOR, for better interop */
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);

		if (0 == pl_casecmp(cuser, &acc->luri.user))
			return ua;
	}

	/* Last resort, try any catchall UAs */
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		if (ua_catchall(ua))
			return ua;
	}

	return NULL;
}


/**
 * Find the correct UA from SIP message
 *
 * @param msg SIP message
 *
 * @return Matching UA if found, NULL if not found
 */
struct ua *uag_find_msg(const struct sip_msg *msg)
{
	struct le *le;
	const struct pl *cuser;
	struct ua *uaf = NULL;  /* fallback ua */

	if (!msg)
		return NULL;

	cuser = &msg->uri.user;
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		if (0 == pl_strcasecmp(cuser, ua_local_cuser(ua))) {
			ua_printf(ua, "selected for %r\n", cuser);
			return ua;
		}
	}

	/* Try also matching by AOR, for better interop and for peer-to-peer
	 * calls */
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);

		if (!acc->regint) {
			if (!uri_match_transport(&acc->luri, NULL, msg->tp))
				continue;

			if (!uri_match_af(&acc->luri, &msg->uri))
				continue;

			if (!uaf && ua_catchall(ua))
				uaf = ua;
		}

		if (0 == pl_casecmp(cuser, &acc->luri.user)) {
			ua_printf(ua, "account match for %r\n", cuser);
			return ua;
		}
	}

	if (uaf)
		ua_printf(uaf, "selected\n");

	return uaf;
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
		struct account *acc = ua_account(ua);

		if (str_isset(aor) && str_cmp(acc->aor, aor))
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
		struct account *acc = ua_account(ua);
		struct sip_addr *laddr = account_laddr(acc);
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
 * Find a User-Agent (UA) best fitting for a SIP request
 *
 * @param requri The SIP uri for the request
 *
 * @return User-Agent (UA) if found, otherwise NULL
 */
struct ua *uag_find_requri(const char *requri)
{
	struct pl pl;

	pl_set_str(&pl, requri);
	return uag_find_requri_pl(&pl);
}


/**
 * Find a User-Agent (UA) best fitting for a SIP request
 *
 * @param requri The SIP uri pointer-length string for the request
 *
 * @return User-Agent (UA) if found, otherwise NULL
 */
struct ua *uag_find_requri_pl(const struct pl *requri)
{
	struct pl pl;
	struct uri *uri;
	struct le *le;
	struct ua *ret = NULL;
	struct sip_addr addr;
	char *uric;
	int err;

	if (!pl_isset(requri))
		return NULL;

	if (!uag.ual.head)
		return NULL;

	err = account_uri_complete_strdup(NULL, &uric, requri);
	if (err)
		goto out;

	pl_set_str(&pl, uric);
	err = sip_addr_decode(&addr, &pl);
	if (err) {
		warning("ua: address %r could not be parsed: %m\n",
			&pl, err);
		goto out;
	}

	uri = &addr.uri;
	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);

		/* not registered */
		if (acc->regint && !ua_isregistered(ua))
			continue;

		if (uri_only_user(uri)) {
			if (acc->regint) {
				ret = ua;
				break;
			}
		}

		if (uri_user_and_host(uri) && acc->regint) {
			if (0 != pl_cmp(&uri->host, &acc->luri.host)) {
				continue;
			}
			else {
				ret = ua;
				break;
			}
		}

		/* Now we select a local account for peer-to-peer calls.
		 * uri = user@IP | user@domain | IP. */
		if (!acc->regint) {
			if (!uri_match_transport(&acc->luri, uri,
						 SIP_TRANSP_NONE))
				continue;

			if (!uri_match_af(&acc->luri, uri))
				continue;

			/* Remember local account.
			 * But we prefer registered UA. */
			if (!ret)
				ret = ua;
		}
	}

	if (ret) {
		ua_printf(ret, "selected for request\n");
	}
	else {
		/* Ok, seems that matching account is missing. */
		if (uri_only_user(uri)) {
			goto out;
		}

		ret = uag.ual.head->data;
		ua_printf(ret, "fallback selection\n");
	}

out:
	mem_deref(uric);
	return ret;
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
 * Counts the calls from all user agents.
 *
 * @return the number of calls over all user agents.
 */
uint32_t uag_call_count(void)
{
	struct le *le;
	uint32_t c = 0;

	for (le = uag.ual.head; le; le = le->next) {
		struct ua *ua = le->data;

		c += list_count(ua_calls(ua));
	}

	return c;
}


int uag_raise(struct ua *ua, struct le *le)
{
	if (!ua || !le)
		return EINVAL;

	list_unlink(le);
	list_prepend(&uag.ual, le, ua);
	return 0;
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
 * Get UAG-TLS Context
 *
 * @return TLS Context if used, NULL otherwise
 */
struct tls *uag_tls(void)
{
#ifdef USE_TLS
	return uag.tls ? uag.tls : NULL;
#else
	return NULL;
#endif
}


/**
 * Setter UAG nodial flag
 *
 * @param nodial
 */
void uag_set_nodial(bool nodial)
{
	uag.nodial = nodial;
}


/**
 * Getter UAG nodial flag
 *
 * @return uag.nodial
 */
bool uag_nodial(void)
{
	return uag.nodial;
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
 * Get extra parameters to use for all SIP Accounts
 *
 * @return Extra parameters
 */
const char *uag_eprm(void)
{
	return uag.eprm;
}


/**
 * Set global Do not Disturb flag
 *
 * @param dnd DnD flag
 */
void uag_set_dnd(bool dnd)
{
	uag.dnd = dnd;
}


/**
 * Get DnD status of uag
 *
 * @return True if DnD is active, False if not
 */
bool uag_dnd(void)
{
	return uag.dnd;
}


/**
 * Enable/Disable a transport protocol
 *
 * @param tp  Transport protocol
 * @param en  true enables the protocol, false disables
 *
 * @return 0 if success, otherwise errorcode
 */
int uag_enable_transport(enum sip_transp tp, bool en)
{

	u32mask_enable(&uag.transports, tp, en);
	return uag_reset_transp(true, true);
}


/**
 * Get the global delayed close flag
 *
 * @return Delayed close flag
 */
bool uag_delayed_close(void)
{
	return uag.delayed_close;
}


/**
 * Get the subscribe handler
 *
 * @return Subscribe handler
 */
sip_msg_h *uag_subh(void)
{
	return uag.subh;
}
