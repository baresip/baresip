/**
 * @file sip/sipsrv.c Mock SIP server
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <string.h>
#include <time.h>
#include <re.h>
#include <baresip.h>
#include "../test.h"
#include "sipsrv.h"


#define DEBUG_MODULE "mock/sipsrv"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


#define LOCAL_PORT        0
#define LOCAL_SECURE_PORT 0
#define EXPIRES_MIN 60
#define EXPIRES_MAX 3600


static int print_contact(struct re_printf *pf, const struct aor *aor)
{
	const uint64_t now = (uint64_t)time(NULL);
	struct le *le;
	int err = 0;

	for (le=aor->locl.head; le; le=le->next) {

		const struct location *loc = le->data;

		if (loc->expires < now)
			continue;

		err |= re_hprintf(pf, "Contact: <%s>;expires=%lli\r\n",
				  loc->uri, loc->expires - now);
	}

	return err;
}


static bool handle_register(struct sip_server *srv, const struct sip_msg *msg)
{
	struct auth auth = { srv, "", false };
	struct sip *sip = srv->sip;
	struct list *lst;
	struct aor *aor;
	struct le *le;
	int err;

	/* Request URI */
	err = domain_find(srv, &msg->uri);
	if (err) {
		if (err == ENOENT) {
			warning("domain not found\n");
			return false;
		}

		sip_reply(sip, msg, 500, strerror(err));
		warning("domain find error: %s\n", strerror(err));
		return true;
	}

	/* Authorize */
	if (srv->auth_enabled)
		err = domain_auth(srv, &msg->to.uri, true,
				  msg, SIP_HDR_AUTHORIZATION, &auth);
	else
		err = domain_find(srv, &msg->to.uri);

	if (err && err != EAUTH) {
		DEBUG_NOTICE("domain auth/find error: %m\n", err);
	}

	switch (err) {

	case 0:
		break;

	case EAUTH:
		sip_replyf(sip, msg, 401, "Unauthorized",
			   "WWW-Authenticate: %H\r\n"
			   "Content-Length: 0\r\n\r\n",
			   auth_print, &auth);
		return true;

	case EPERM:
		sip_reply(sip, msg, 403, "Forbidden");
		return true;

	case ENOENT:
		sip_reply(sip, msg, 404, "Not Found");
		return true;

	default:
		sip_reply(sip, msg, 500, strerror(err));
		warning("domain error: %s\n", strerror(err));
		return true;
	}

	/* Find AoR */
	err = aor_find(srv, &aor, &msg->to.uri);
	if (err) {
		if (err != ENOENT) {
			sip_reply(sip, msg, 500, strerror(err));
			warning("aor find error: %s\n", strerror(err));
			return true;
		}

		err = aor_create(srv, &aor, &msg->to.uri);
		if (err) {
			sip_reply(sip, msg, 500, strerror(err));
			warning("aor create error: %s\n", strerror(err));
			return true;
		}
	}

	/* Process Contacts */
	lst = hash_list(msg->hdrht, SIP_HDR_CONTACT);

	for (le=list_head(lst); le; le=le->next) {

		const struct sip_hdr *hdr = le->data;
		struct sip_addr contact;
		uint32_t expires;
		struct pl pl;

		if (hdr->id != SIP_HDR_CONTACT)
			continue;

		err = sip_addr_decode(&contact, &hdr->val);
		if (err) {
			sip_reply(sip, msg, 400, "Bad Contact");
			goto fail;
		}

		if (!msg_param_decode(&contact.params, "expires", &pl))
			expires = pl_u32(&pl);
		else if (pl_isset(&msg->expires))
			expires = pl_u32(&msg->expires);
		else
			expires = 3600;

		if (expires > 0 && expires < EXPIRES_MIN) {
			sip_replyf(sip, msg, 423, "Interval Too Brief",
				   "Min-Expires: %u\r\n"
				   "Content-Length: 0\r\n\r\n",
				   EXPIRES_MIN);
			goto fail;
		}

		expires = min(expires, EXPIRES_MAX);

		err = location_update(&aor->locl, msg, &contact, expires);
		if (err) {
			sip_reply(sip, msg, 500, strerror(err));
			if (err != EPROTO)
				warning("location update error: %s\n",
				      strerror(err));
			goto fail;
		}
	}

	location_commit(&aor->locl);

	sip_treplyf(NULL, NULL, sip, msg, false, 200, "OK",
		    "%H"
		    "Date: %H\r\n"
		    "Content-Length: 0\r\n\r\n",
		    print_contact, aor,
		    fmt_gmtime, NULL);

	return true;

 fail:
	location_rollback(&aor->locl);

	return true;
}


static bool sip_msg_handler(const struct sip_msg *msg, void *arg)
{
	struct sip_server *srv = arg;
	int err = 0;

#if 0
	DEBUG_NOTICE("[%u] recv %r via %s\n", srv->instance,
		     &msg->met, sip_transp_name(msg->tp));
#endif

	srv->tp_last = msg->tp;

	if (0 == pl_strcmp(&msg->met, "REGISTER")) {
		++srv->n_register_req;
		if (handle_register(srv, msg))
			goto out;

		sip_reply(srv->sip, msg, 503, "Server Error");
	}
	else {
		DEBUG_NOTICE("method not handled (%r)\n", &msg->met);
		return false;
	}

	if (srv->terminate)
		err = sip_reply(srv->sip, msg, 503, "Server Error");

	if (err) {
		DEBUG_WARNING("could not reply: %m\n", err);
	}

 out:
	if (srv->terminate) {

		if (srv->exith)
			srv->exith(srv->arg);
	}

	return true;
}


static void destructor(void *arg)
{
	struct sip_server *srv = arg;

	srv->terminate = true;

	sip_close(srv->sip, false);
	mem_deref(srv->sip);

	hash_flush(srv->ht_aor);
	mem_deref(srv->ht_aor);

	hash_flush(srv->ht_dom);
	mem_deref(srv->ht_dom);
}


int sip_server_alloc(struct sip_server **srvp,
		     sip_exit_h *exith, void *arg)
{
	struct sip_server *srv;
	struct sa laddr, laddrs;
	struct tls *tls = NULL;
	int err;

	if (!srvp)
		return EINVAL;

	srv = mem_zalloc(sizeof *srv, destructor);
	if (!srv)
		return ENOMEM;

	err  = sa_set_str(&laddr,  "127.0.0.1", LOCAL_PORT);
	err |= sa_set_str(&laddrs, "127.0.0.1", LOCAL_SECURE_PORT);
	if (err)
		goto out;

	err = sip_alloc(&srv->sip, NULL, 16, 16, 16,
			"mock SIP server", NULL, NULL);
	if (err)
		goto out;

	err |= sip_transp_add(srv->sip, SIP_TRANSP_UDP, &laddr);
	err |= sip_transp_add(srv->sip, SIP_TRANSP_TCP, &laddr);
	if (err)
		goto out;

#ifdef USE_TLS
	err = tls_alloc(&tls, TLS_METHOD_SSLV23, NULL, NULL);
	if (err)
		goto out;

	err = tls_set_certificate(tls, test_certificate,
				  strlen(test_certificate));
	if (err)
		goto out;

	err |= sip_transp_add(srv->sip, SIP_TRANSP_TLS, &laddrs, tls);
#endif
	if (err)
		goto out;

	err = sip_listen(&srv->lsnr, srv->sip, true, sip_msg_handler, srv);
	if (err)
		goto out;

	srv->secret = rand_u64();

	err = hash_alloc(&srv->ht_dom, 32);
	if (err)
		goto out;

	err = hash_alloc(&srv->ht_aor, 32);
	if (err)
		goto out;

	srv->exith = exith;
	srv->arg = arg;

 out:
	mem_deref(tls);
	if (err)
		mem_deref(srv);
	else
		*srvp = srv;

	return err;
}


int sip_server_uri(struct sip_server *srv, char *uri, size_t sz,
		   enum sip_transp tp)
{
	struct sa laddr;
	int err;

	if (!srv || !uri || !sz)
		return EINVAL;

	err = sip_transp_laddr(srv->sip, &laddr, tp, NULL);
	if (err)
		return err;

	/* NOTE: angel brackets needed to parse ;transport parameter */
	if (re_snprintf(uri, sz, "<sip:x@%J%s>",
			&laddr, sip_transp_param(tp)) < 0)
		return ENOMEM;

	return 0;
}
