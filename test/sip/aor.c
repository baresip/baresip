/**
 * @file sip/aor.c Mock SIP server -- SIP Address of Record
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include "sipsrv.h"


static void destructor(void *arg)
{
	struct aor *aor = arg;

	list_flush(&aor->locl);
	hash_unlink(&aor->he);
	mem_deref(aor->uri);
}


static int uri_canon(char **curip, const struct uri *uri)
{
	if (pl_isset(&uri->user))
		return re_sdprintf(curip, "%r:%H@%r",
				   &uri->scheme,
				   uri_user_unescape, &uri->user,
				   &uri->host);
	else
		return re_sdprintf(curip, "%r:%r",
				   &uri->scheme,
				   &uri->host);
}


int aor_create(struct sip_server *srv, struct aor **aorp,
	       const struct uri *uri)
{
	struct aor *aor;
	int err;

	if (!aorp || !uri)
		return EINVAL;

	aor = mem_zalloc(sizeof(*aor), destructor);
	if (!aor)
		return ENOMEM;

	err = uri_canon(&aor->uri, uri);
	if (err)
		goto out;

	hash_append(srv->ht_aor, hash_joaat_str_ci(aor->uri), &aor->he, aor);

 out:
	if (err)
		mem_deref(aor);
	else
		*aorp = aor;

	return err;
}


int aor_find(struct sip_server *srv, struct aor **aorp, const struct uri *uri)
{
	struct list *lst;
	struct aor *aor = NULL;
	struct le *le;
	char *curi;
	int err;

	if (!uri)
		return EINVAL;

	err = uri_canon(&curi, uri);
	if (err)
		return err;

	lst = hash_list(srv->ht_aor, hash_joaat_str_ci(curi));

	for (le=list_head(lst); le; le=le->next) {

		aor = le->data;

		if (!str_casecmp(curi, aor->uri))
			break;
	}

	mem_deref(curi);

	if (!le)
		return ENOENT;

	if (aorp)
		*aorp = aor;

	return 0;
}
