/**
 * @file sip/location.c Mock SIP server -- location handling
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <time.h>
#include <re.h>
#include "sipsrv.h"


struct loctmp {
	struct sa src;
	struct uri duri;
	char *uri;
	char *callid;
	uint32_t expires;
	uint32_t cseq;
	double q;
};


static void destructor_loctmp(void *arg)
{
	struct loctmp *tmp = arg;

	mem_deref(tmp->uri);
	mem_deref(tmp->callid);
}


static void destructor_location(void *arg)
{
	struct location *loc = arg;

	list_unlink(&loc->le);
	mem_deref(loc->uri);
	mem_deref(loc->callid);
	mem_deref(loc->tmp);
}


static int param_handler(const struct pl *pname, const struct pl *pvalue,
			 void *arg)
{
	struct pl *other_params = arg;
	struct pl other_pvalue = PL_INIT;
	bool both;

	if (0 == pl_strcmp(pname, "user"))
		both = true;
	else if (0 == pl_strcmp(pname, "ttl"))
		both = true;
	else if (0 == pl_strcmp(pname, "method"))
		both = true;
	else if (0 == pl_strcmp(pname, "maddr"))
		both = true;
	else if (0 == pl_strcmp(pname, "transport"))
		both = true;
	else
		both = false;

	if (uri_param_get(other_params, pname, &other_pvalue))
		return both ? ENOENT : 0;

	return pl_casecmp(pvalue, &other_pvalue);
}


static int header_handler(const struct pl *hname, const struct pl *hvalue,
			  void *arg)
{
	struct pl *other_headers = arg;
	struct pl other_hvalue;
	int err;

	err = uri_header_get(other_headers, hname, &other_hvalue);
	if (err)
		return err;

	return pl_casecmp(hvalue, &other_hvalue);
}


/**
 * Compare two URIs - see RFC 3261 Section 19.1.4
 *
 * @param l  Left-hand URI object
 * @param r  Right-hand URI object
 *
 * @return true if match, otherwise false
 */
static bool my_uri_cmp(const struct uri *l, const struct uri *r)
{
	int err;

	if (!l || !r)
		return false;

	if (l == r)
		return true;

	/* A SIP and SIPS URI are never equivalent. */
	if (pl_casecmp(&l->scheme, &r->scheme))
		return false;

	/* Comparison of the userinfo of SIP and SIPS URIs is case-sensitive */
	if (pl_cmp(&l->user, &r->user))
		return false;

	if (pl_cmp(&l->password, &r->password))
		return false;

	if (pl_casecmp(&l->host, &r->host))
		return false;
	if (l->af != r->af)
		return false;

	if (l->port != r->port)
		return false;

	/* URI parameters */
	err = uri_params_apply(&l->params, param_handler, (void *)&r->params);
	if (err)
		return false;
	err = uri_params_apply(&r->params, param_handler, (void *)&l->params);
	if (err)
		return false;

	/* URI headers */
	err = uri_headers_apply(&l->headers, header_handler,
				(void *)&r->headers);
	if (err)
		return false;
	err = uri_headers_apply(&r->headers, header_handler,
				(void *)&l->headers);
	if (err)
		return false;

	/* Match */
	return true;
}


static bool cmp_handler(struct le *le, void *arg)
{
	struct location *loc = le->data;

	return my_uri_cmp(&loc->duri, arg);
}


int location_update(struct list *locl, const struct sip_msg *msg,
		    const struct sip_addr *contact, uint32_t expires)
{
	struct location *loc, *loc_new = NULL;
	struct loctmp *tmp;
	struct pl pl;
	int err;

	if (!locl || !msg || !contact)
		return EINVAL;

	loc = list_ledata(list_apply(locl, true, cmp_handler,
				     (void *)&contact->uri));
	if (!loc) {
		if (expires == 0)
			return 0;

		loc = loc_new = mem_zalloc(sizeof(*loc), destructor_location);
		if (!loc)
			return ENOMEM;

		list_append(locl, &loc->le, loc);
	}
	else {
		if (!pl_strcmp(&msg->callid, loc->callid) &&
		    msg->cseq.num <= loc->cseq)
			return EPROTO;

		if (expires == 0) {
			loc->rm = true;
			return 0;
		}
	}

	tmp = mem_zalloc(sizeof(*tmp), destructor_loctmp);
	if (!tmp) {
		err = ENOMEM;
		goto out;
	}

	err = pl_strdup(&tmp->uri, &contact->auri);
	if (err)
		goto out;

	pl_set_str(&pl, tmp->uri);

	if (uri_decode(&tmp->duri, &pl)) {
		err = EBADMSG;
		goto out;
	}

	err = pl_strdup(&tmp->callid, &msg->callid);
	if (err)
		goto out;


	if (!msg_param_decode(&contact->params, "q", &pl))
		tmp->q = pl_float(&pl);
	else
		tmp->q = 1;

	tmp->cseq    = msg->cseq.num;
	tmp->expires = expires;
	tmp->src     = msg->src;

 out:
	if (err) {
		mem_deref(loc_new);
		mem_deref(tmp);
	}
	else {
		mem_deref(loc->tmp);
		loc->tmp = tmp;
	}

	return err;
}


void location_commit(struct list *locl)
{
	time_t now = time(NULL);
	struct le *le;

	if (!locl)
		return;

	for (le=locl->head; le; ) {

		struct location *loc = le->data;

		le = le->next;

		if (loc->rm) {
			list_unlink(&loc->le);
			mem_deref(loc);
		}
		else if (loc->tmp) {

			mem_deref(loc->uri);
			mem_deref(loc->callid);

			loc->uri       = mem_ref(loc->tmp->uri);
			loc->callid    = mem_ref(loc->tmp->callid);
			loc->duri      = loc->tmp->duri;
			loc->cseq      = loc->tmp->cseq;
			loc->expires   = loc->tmp->expires + now;
			loc->src       = loc->tmp->src;
			loc->q         = loc->tmp->q;

			loc->tmp = mem_deref(loc->tmp);
		}
	}
}


void location_rollback(struct list *locl)
{
	struct le *le;

	if (!locl)
		return;

	for (le=locl->head; le; ) {

		struct location *loc = le->data;

		le = le->next;

		if (!loc->uri) {
			list_unlink(&loc->le);
			mem_deref(loc);
		}
		else {
			loc->tmp = mem_deref(loc->tmp);
			loc->rm  = false;
		}
	}
}
