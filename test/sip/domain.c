/**
 * @file sip/domain.c Mock SIP server -- domain handling
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <re.h>
#include "sipsrv.h"


#define DEBUG_MODULE "mock/sipsrv"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


enum {
	NONCE_EXPIRES = 300,
};


static void destructor(void *arg)
{
	struct domain *dom = arg;

	hash_unlink(&dom->he);
	hash_flush(dom->ht_usr);
	mem_deref(dom->ht_usr);
	mem_deref(dom->name);
}


static struct domain *lookup(struct sip_server *srv, const struct pl *name)
{
	struct list *lst;
	struct le *le;

	lst = hash_list(srv->ht_dom, hash_joaat_ci(name->p, name->l));

	for (le=list_head(lst); le; le=le->next) {

		struct domain *dom = le->data;

		if (pl_strcasecmp(name, dom->name))
			continue;

		return dom;
	}

	return NULL;
}


int domain_add(struct sip_server *srv, const char *name)
{
	struct domain *dom;
	int err;

	dom = mem_zalloc(sizeof(*dom), destructor);
	if (!dom)
		return ENOMEM;

	err = str_dup(&dom->name, name);
	if (err)
		goto out;

	err = hash_alloc(&dom->ht_usr, 32);
	if (err)
		return err;

	hash_append(srv->ht_dom, hash_joaat_str_ci(name), &dom->he, dom);

 out:
	if (err)
		mem_deref(dom);

	return err;
}


int domain_find(struct sip_server *srv, const struct uri *uri)
{
	int err = ENOENT;
	struct sa addr;

	if (!uri)
		return EINVAL;

	if (!sa_set(&addr, &uri->host, uri->port)) {

		if (!uri->port) {

			uint16_t port = SIP_PORT;

			if (!pl_strcasecmp(&uri->scheme, "sips"))
				port = SIP_PORT_TLS;

			sa_set_port(&addr, port);
		}

		if (sip_transp_isladdr(srv->sip, SIP_TRANSP_NONE, &addr))
			return 0;

		return ENOENT;
	}

	err = lookup(srv, &uri->host) ? 0 : ENOENT;

	return err;
}


int domain_auth(struct sip_server *srv,
		const struct uri *uri, bool user_match,
		const struct sip_msg *msg, enum sip_hdrid hdrid,
		struct auth *auth)
{
	struct domain *dom;
	struct list *lst;
	struct le *le;
	int err = ENOENT;

	if (!uri || !msg || !auth)
		return EINVAL;

	dom = lookup(srv, &uri->host);
	if (!dom) {
		DEBUG_WARNING("domain not found (%r)\n", &uri->host);
		return ENOENT;
	}

	err = auth_set_realm(auth, dom->name);
	if (err)
		return err;

	auth->stale = false;

	lst = hash_list(msg->hdrht, hdrid);

	for (le=list_head(lst); le; le=le->next) {

		const struct sip_hdr *hdr = le->data;
		struct httpauth_digest_resp resp;
		const struct user *usr;

		if (hdr->id != hdrid)
			continue;

		if (httpauth_digest_response_decode(&resp, &hdr->val))
			continue;

		if (pl_strcasecmp(&resp.realm, dom->name))
			continue;

		if (auth_chk_nonce(srv, &resp.nonce, NONCE_EXPIRES)) {
			auth->stale = true;
			continue;
		}

		auth->stale = false;

		usr = user_find(dom->ht_usr, &resp.username);
		if (!usr) {
			DEBUG_WARNING("user not found (%r)\n", &resp.username);
			break;
		}

		err = httpauth_digest_response_auth(&resp, &msg->met,
						    user_ha1(usr));
		if (err)
			return err;

		if (user_match && pl_cmp(&resp.username, &uri->user))
			return EPERM;

		return 0;
	}

	return EAUTH;
}


struct domain *domain_lookup(struct sip_server *srv, const char *name)
{
	struct pl pl;

	pl_set_str(&pl, name);

	return lookup(srv, &pl);
}
