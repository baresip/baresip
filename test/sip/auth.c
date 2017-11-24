/**
 * @file sip/auth.c Mock SIP server -- authentication
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <string.h>
#include <time.h>
#include <re.h>
#include "sipsrv.h"


enum {
	NONCE_MIN_SIZE = 33,
};


int auth_print(struct re_printf *pf, const struct auth *auth)
{
	uint8_t key[MD5_SIZE];
	uint64_t nv[2];

	if (!auth)
		return EINVAL;

	nv[0] = time(NULL);
	nv[1] = auth->srv->secret;

	md5((uint8_t *)nv, sizeof(nv), key);

	return re_hprintf(pf,
			  "Digest realm=\"%s\", nonce=\"%w%llx\", "
			  "qop=\"auth\"%s",
			  auth->realm,
			  key, sizeof(key), nv[0],
			  auth->stale ? ", stale=true" : "");
}


int auth_chk_nonce(struct sip_server *srv,
		   const struct pl *nonce, uint32_t expires)
{
	uint8_t nkey[MD5_SIZE], ckey[MD5_SIZE];
	uint64_t nv[2];
	struct pl pl;
	int64_t age;
	unsigned i;

	if (!nonce || !nonce->p || nonce->l < NONCE_MIN_SIZE)
		return EINVAL;

	pl = *nonce;

	for (i=0; i<sizeof(nkey); i++) {
		nkey[i]  = ch_hex(*pl.p++) << 4;
		nkey[i] += ch_hex(*pl.p++);
		pl.l -= 2;
	}

	nv[0] = pl_x64(&pl);
	nv[1] = srv->secret;

	md5((uint8_t *)nv, sizeof(nv), ckey);

	if (memcmp(nkey, ckey, MD5_SIZE))
		return EAUTH;

	age = time(NULL) - nv[0];

	if (age < 0 || age > expires)
		return ETIMEDOUT;

	return 0;
}


int auth_set_realm(struct auth *auth, const char *realm)
{
	size_t len;

	if (!auth || !realm)
		return EINVAL;

	len = strlen(realm);
	if (len >= sizeof(auth->realm))
		return ENOMEM;

	memcpy(auth->realm, realm, len);
	auth->realm[len] = '\0';

	return 0;
}
