/**
 * @file sip/sipsrv.c Mock SIP server
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#include <stdint.h>
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


static bool enc_list_handler(const struct sip_hdr *hdr,
			     const struct sip_msg *msg, void *arg)
{
	struct mbuf *mb = arg;
	(void)msg;

	int err = mbuf_printf(mb, "%s%r", mb->end ? "," : "", &hdr->val);
	if (err)
		return true;

	return false;
}


static int sip_msg_hdr_encode_list(const struct sip_msg *msg,
				   enum sip_hdrid id, struct mbuf *mb)
{
	const struct sip_hdr *hdr;
	struct mbuf *values;
	int err;

	hdr = sip_msg_hdr(msg, id);
	if (!hdr)
		return 0;

	values = mbuf_alloc(16);
	if (!values)
		return ENOMEM;

	sip_msg_hdr_apply(msg, true, id, enc_list_handler, values);

	values->pos = 0;
	err = mbuf_printf(mb, "%r: %b\r\n", &hdr->name, mbuf_buf(values),
			  mbuf_get_left(values));
	mem_deref(values);
	return err;
}


struct hdr_handler_arg {
	struct mbuf *mb;
	int err;
};


static bool enc_handler(const struct sip_hdr *hdr, const struct sip_msg *msg,
			void *arg)
{
	struct hdr_handler_arg *harg = arg;
	struct mbuf *mb = harg->mb;

	if (hdr->id == SIP_HDR_VIA && !msg->req)
		return false;

	harg->err = mbuf_printf(mb, "%r: %r\r\n", &hdr->name, &hdr->val);
	if (harg->err)
		return true;

	return false;
}


struct via_handler_arg {
	struct mbuf *mb;
	int err;
	struct sa *dst;
	uint32_t idx;
};


static bool reply_via_handler(const struct sip_hdr *hdr,
			      const struct sip_msg *msg, void *arg)
{
	struct via_handler_arg *vharg = arg;
	struct mbuf *mb = vharg->mb;
	struct sa *dst = vharg->dst;
	int err = 0;

	struct sip_via via;
	sip_via_decode(&via, &hdr->val);

	/* remove own Via header */
	if (vharg->idx==0 && sa_cmp(&via.addr, &msg->dst, SA_ALL)) {
		goto out;
	}
	else if (vharg->idx==0) {
		DEBUG_WARNING("top Via of reply does not match (%J vs %J)\n",
			      &via.addr, &msg->dst);
		err = EINVAL;
		goto out;
	}

	/* get dst address from next Via */
	if (!sa_isset(dst, SA_ADDR))
		*dst = via.addr;

	err = mbuf_printf(mb, "%r: %r\r\n", &hdr->name, &hdr->val);
	if (err)
		goto out;

out:
	++vharg->idx;
	if (err) {
		vharg->err = err;
		return true;  /* stop processing */
	}

	return false;
}


static int sip_req_forward(struct sip_server *srv, const struct sip_msg *msg,
			   struct mbuf *mb, struct sa *dst)
{
	struct aor *aor;
	int err;

	/* use request URI to find contact aor */
	err = aor_find(srv, &aor, &msg->uri);
	if (err) {
		DEBUG_WARNING("aor not found (%r)\n", &msg->uri);
		return err;
	}

	/* use first contact of aor */
	struct location *loc = list_ledata(list_head(&aor->locl));
	if (!loc) {
		DEBUG_WARNING("aor missing (%r)\n", &msg->uri);
		return err;
	}

	/* the contact aor needs to be an IP address + port number */
	struct uri duri = loc->duri;
	err = sa_set(dst, &duri.host, duri.port);
	if (err)
		return err;

	struct sa laddr;
	sip_transp_laddr(srv->sip, &laddr, msg->tp, dst);

	struct hdr_handler_arg harg = {
		.mb = mb,
		.err = 0,
	};

	err  = mbuf_printf(mb, "%r %H SIP/2.0\r\n", &msg->met,
			   uri_encode, &duri);
	err |= mbuf_printf(mb, "Via: SIP/2.0/%s %J;"
			   "branch=z9hG4bK%016llx;rport\r\n",
			   sip_transp_name(msg->tp), &laddr,
			   rand_u64());
	sip_msg_hdr_apply(msg, true, SIP_HDR_VIA, enc_handler, &harg);
	if (harg.err) {
		err |= harg.err;
		return err;
	}

	const struct sip_hdr *maxfwd = sip_msg_hdr(msg, SIP_HDR_MAX_FORWARDS);
	if (maxfwd) {
		uint32_t mf = pl_u32(&maxfwd->val);
		if (mf == 0) {
			DEBUG_WARNING("Max-Forwards is zero\n");
			return EPROTO;
		}

		err |= mbuf_printf(mb, "%r: %u\r\n", &maxfwd->name, mf - 1);
	}
	else {
		err |= mbuf_printf(mb, "Max-Forwards: 70\r\n");
	}

	if (err) {
		DEBUG_WARNING("could not forward SIP request %r\n", &msg->met);
		return err;
	}

	DEBUG_INFO("forwarding SIP request %r from %J via %J to %J\n",
		   &msg->met, &msg->src, &laddr, dst);
	return 0;
}


static int sip_reply_forward(struct sip_server *srv, const struct sip_msg *msg,
			     struct mbuf *mb, struct sa *dst)
{
	int err;

	if (!srv || !msg)
		return EINVAL;

	/* Get aor from registration */

	struct mbuf *viamb = mbuf_alloc(32);
	if (!viamb)
		return ENOMEM;

	struct via_handler_arg vharg = {
		.mb = viamb,
		.dst = dst,
		.idx = 0,
		.err = 0
	};

	sip_msg_hdr_apply(msg, true, SIP_HDR_VIA, reply_via_handler, &vharg);
	if (vharg.err) {
		err = vharg.err;
		goto out;
	}

	viamb->pos = 0;
	err  = mbuf_printf(mb, "SIP/2.0 %u %r\r\n", msg->scode, &msg->reason);
	err |= mbuf_printf(mb, "%b", mbuf_buf(viamb), mbuf_get_left(viamb));
out:
	mem_deref(viamb);
	if (err) {
		DEBUG_WARNING("could not forward SIP reply %u %r (%m)\n",
			      msg->scode, &msg->reason, err);
		return err;
	}

	DEBUG_INFO("forwarding SIP reply %u %r from %J via %J to %J\n",
		   msg->scode, &msg->reason, &msg->src, &msg->dst, dst);
	return 0;
}


static bool forward_msg(struct sip_server *srv, const struct sip_msg *msg)
{
	int err;

	/* Request URI */
	if (msg->req) {
		err = domain_find(srv, &msg->uri);
		if (err)
			return false;
	}

	struct mbuf *mb = mbuf_alloc(1024);
	if (!mb)
		return ENOMEM;

	/* Forward msg */
	struct sa dst = {0};
	if (msg->req)
		err = sip_req_forward(srv, msg, mb, &dst);
	else
		err = sip_reply_forward(srv, msg, mb, &dst);

	struct hdr_handler_arg harg = {
		.mb = mb,
		.err = 0,
	};

	sip_msg_hdr_apply(msg, true, SIP_HDR_CONTACT, enc_handler, &harg);
	sip_msg_hdr_apply(msg, true, SIP_HDR_FROM, enc_handler, &harg);
	sip_msg_hdr_apply(msg, true, SIP_HDR_TO, enc_handler, &harg);
	sip_msg_hdr_apply(msg, true, SIP_HDR_CALL_ID, enc_handler, &harg);
	sip_msg_hdr_apply(msg, true, SIP_HDR_CSEQ, enc_handler, &harg);
	sip_msg_hdr_apply(msg, true, SIP_HDR_USER_AGENT, enc_handler, &harg);
	err |= sip_msg_hdr_encode_list(msg, SIP_HDR_ALLOW, mb);
	err |= sip_msg_hdr_encode_list(msg, SIP_HDR_SUPPORTED, mb);
	sip_msg_hdr_apply(msg, true, SIP_HDR_CONTENT_TYPE, enc_handler,	&harg);
	sip_msg_hdr_apply(msg, true, SIP_HDR_CONTENT_LENGTH, enc_handler,
			  &harg);
	/* append the SIP message body */
	const struct sip_hdr *hdr = sip_msg_hdr(msg, SIP_HDR_CONTENT_LENGTH);
	err |= mbuf_printf(mb, "\r\n");
	uint32_t clen = hdr ? pl_u32(&hdr->val) : 0;
	if (clen) {
		err |= mbuf_printf(mb, "%b", mbuf_buf(msg->mb),
				   mbuf_get_left(msg->mb));
	}

	if (harg.err)
		err = harg.err;

	if (err)
		goto out;

	mbuf_set_pos(mb, 0);
	err = sip_send_conn(srv->sip, NULL, msg->tp, &dst, NULL, mb, NULL,
			    NULL);
	if (err)
		goto out;

	DEBUG_INFO("successfully forwarded SIP message\n");
out:
	mem_deref(mb);
	return err == 0;
}


static bool sip_msg_handler(const struct sip_msg *msg, void *arg)
{
	struct sip_server *srv = arg;
	int err = 0;

#if 0
	if (msg->req) {
		DEBUG_NOTICE("[%u] recv %r via %s\n", srv->instance,
			     &msg->met, sip_transp_name(msg->tp));
	}
	else {
		DEBUG_NOTICE("[%u] recv %u %r via %s\n", srv->instance,
			     msg->scode, &msg->reason,
			     sip_transp_name(msg->tp));
	}
#endif

	srv->tp_last = msg->tp;

	if (0 == pl_strcmp(&msg->met, "REGISTER")) {
		++srv->n_register_req;
		if (handle_register(srv, msg))
			goto out;

		sip_reply(srv->sip, msg, 503, "Server Error");
	}
	else {
		if (forward_msg(srv, msg))
			goto out;

		sip_reply(srv->sip, msg, 503, "Server Error");
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

	srv = mem_zalloc(sizeof(*srv), destructor);
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

	err = sip_listen(&srv->lsnr, srv->sip, false, sip_msg_handler, srv);
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
