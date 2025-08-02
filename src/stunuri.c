/**
 * @file stunuri.c URI Scheme for STUN/TURN Protocol
 *
 * Copyright (C) 2010 Alfred E. Heggestad
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
	struct uri uri;
	int err;

	if (!sup || !pl)
		return EINVAL;

	err = uri_decode(&uri, pl);
	if (err) {
		warning("stunuri: decode '%r' failed (%m)\n", pl, err);
		return err;
	}

	err = stunuri_decode_uri(sup, &uri);

	return err;
}

/**
 * Decode a STUN uri from URI object
 *
 * @param sup Pointer to allocated STUN uri
 * @param uri URI object
 *
 * @return 0 if success, otherwise errorcode
 */
int stunuri_decode_uri(struct stun_uri **sup, const struct uri *uri)
{
	struct stun_uri *su;
	enum stun_scheme scheme;
	struct pl tp;
	int proto = IPPROTO_UDP;
	int err;

	if (!sup || !uri)
		return EINVAL;

	if (0 == pl_strcasecmp(&uri->scheme, "stun"))
		scheme = STUN_SCHEME_STUN;
	else if (0 == pl_strcasecmp(&uri->scheme, "stuns"))
		scheme = STUN_SCHEME_STUNS;
	else if (0 == pl_strcasecmp(&uri->scheme, "turn"))
		scheme = STUN_SCHEME_TURN;
	else if (0 == pl_strcasecmp(&uri->scheme, "turns"))
		scheme = STUN_SCHEME_TURNS;
	else {
		warning("stunuri: scheme not supported (%r)\n", &uri->scheme);
		return ENOTSUP;
	}

	if (0 == re_regex(uri->headers.p, uri->headers.l,
		"transport=[a-z]+", &tp)) {

		if (0 == pl_strcasecmp(&tp, "udp"))
			proto = IPPROTO_UDP;
		else if (0 == pl_strcasecmp(&tp, "tcp"))
			proto = IPPROTO_TCP;
		else {
			warning("stunuri: unsupported transport '%r'\n", &tp);
			return EPROTONOSUPPORT;
		}
	}

	su = mem_zalloc(sizeof(*su), destructor);
	if (!su)
		return ENOMEM;

	su->scheme = scheme;
	err = pl_strdup(&su->host, &uri->host);
	su->port = uri->port;
	su->proto = proto;

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
	err |= re_hprintf(pf, " proto=%s", net_proto2name(su->proto));

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
