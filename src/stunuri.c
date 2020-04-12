/**
 * @file stunuri.c URI Scheme for STUN/TURN Protocol
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <re.h>
#include <baresip.h>
#include "core.h"


/*
  https://tools.ietf.org/html/rfc7064
  https://tools.ietf.org/html/rfc7065


                          +-----------------------+
                          | URI                   |
                          +-----------------------+
                          | stun:example.org      |
                          | stuns:example.org     |
                          | stun:example.org:8000 |
                          +-----------------------+


   +---------------------------------+----------+--------+-------------+
   | URI                             | <secure> | <port> | <transport> |
   +---------------------------------+----------+--------+-------------+
   | turn:example.org                | false    |        |             |
   | turns:example.org               | true     |        |             |
   | turn:example.org:8000           | false    | 8000   |             |
   | turn:example.org?transport=udp  | false    |        | UDP         |
   | turn:example.org?transport=tcp  | false    |        | TCP         |
   | turns:example.org?transport=tcp | true     |        | TLS         |
   +---------------------------------+----------+--------+-------------+

 */


static void destructor(void *arg)
{
	struct stun_uri *su = arg;

	mem_deref(su->host);
}


/**
 * Decode a STUN uri from a string
 *
 * @param sup Pointer to allocated STUN uri
 * @param pl  Pointer-length string
 *
 * @return 0 if success, otherwise errorcode
 */
int stunuri_decode(struct stun_uri **sup, const struct pl *pl)
{
	struct stun_uri *su;
	struct uri uri;
	enum stun_scheme scheme;
	int err;

	if (!sup || !pl)
		return EINVAL;

	err = uri_decode(&uri, pl);
	if (err) {
		warning("stunuri: decode '%r' failed (%m)\n", pl, err);
		return err;
	}

	if (0 == pl_strcasecmp(&uri.scheme, "stun"))
		scheme = STUN_SCHEME_STUN;
	else if (0 == pl_strcasecmp(&uri.scheme, "stuns"))
		scheme = STUN_SCHEME_STUNS;
	else if (0 == pl_strcasecmp(&uri.scheme, "turn"))
		scheme = STUN_SCHEME_TURN;
	else if (0 == pl_strcasecmp(&uri.scheme, "turns"))
		scheme = STUN_SCHEME_TURNS;
	else {
		warning("stunuri: scheme not supported (%r)\n", &uri.scheme);
		return ENOTSUP;
	}

	su = mem_zalloc(sizeof(*su), destructor);
	if (!su)
		return ENOMEM;

	su->scheme = scheme;
	err = pl_strdup(&su->host, &uri.host);
	su->port = uri.port;

	if (err)
		mem_deref(su);
	else
		*sup = su;

	return err;
}


/**
 * Set the hostname on a STUN uri
 *
 * @param su   STUN uri
 * @param host Hostname to set
 *
 * @return 0 if success, otherwise errorcode
 */
int stunuri_set_host(struct stun_uri *su, const char *host)
{
	if (!su || !host)
		return EINVAL;

	su->host = mem_deref(su->host);

	return str_dup(&su->host, host);
}


/**
 * Set the port number on a STUN uri
 *
 * @param su   STUN uri
 * @param port Port number to set
 *
 * @return 0 if success, otherwise errorcode
 */
int stunuri_set_port(struct stun_uri *su, uint16_t port)
{
	if (!su)
		return EINVAL;

	su->port = port;

	return 0;
}


/**
 * Print a STUN uri
 *
 * @param pf Print function
 * @param su STUN uri
 *
 * @return 0 if success, otherwise errorcode
 */
int stunuri_print(struct re_printf *pf, const struct stun_uri *su)
{
	int err = 0;

	if (!su)
		return 0;

	err |= re_hprintf(pf, "scheme=%s", stunuri_scheme_name(su->scheme));
	err |= re_hprintf(pf, " host='%s'", su->host);
	err |= re_hprintf(pf, " port=%u", su->port);

	return err;
}


/**
 * Get the name of the STUN scheme
 *
 * @param scheme STUN scheme
 *
 * @return String with STUN scheme
 */
const char *stunuri_scheme_name(enum stun_scheme scheme)
{
	switch (scheme) {

	case STUN_SCHEME_STUN:  return "stun";
	case STUN_SCHEME_STUNS: return "stuns";
	case STUN_SCHEME_TURN:  return "turn";
	case STUN_SCHEME_TURNS: return "turns";
	default: return "?";
	}
}
