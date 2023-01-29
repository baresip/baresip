/**
 * @file test/stunuri.c  Baresip selftest -- STUN uri
 *
 * Copyright (C) 2010 - 2021 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "stunuri"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


int test_stunuri(void)
{
	static const struct test {
		const char *uri;
		enum stun_scheme scheme;
		const char *host;
		uint16_t port;
		int proto;
	} testv[] = {
		{
			"stun:example.org",
			STUN_SCHEME_STUN, "example.org", 0, IPPROTO_UDP
		},
		{
			"stuns:example.org",
			STUN_SCHEME_STUNS, "example.org", 0, IPPROTO_UDP
		},
		{
			"stun:example.org:8000",
			STUN_SCHEME_STUN, "example.org", 8000, IPPROTO_UDP
		},
		{
			"turn:example.org",
			STUN_SCHEME_TURN, "example.org", 0, IPPROTO_UDP
		},
		{
			"turns:example.org",
			STUN_SCHEME_TURNS, "example.org", 0, IPPROTO_UDP
		},
		{
			"turn:example.org:8000",
			STUN_SCHEME_TURN, "example.org", 8000, IPPROTO_UDP
		},
		{
			"turn:example.org?transport=udp",
			STUN_SCHEME_TURN, "example.org", 0, IPPROTO_UDP
		},
		{
			"turn:example.org?transport=tcp",
			STUN_SCHEME_TURN, "example.org", 0, IPPROTO_TCP
		}
	};
	struct stun_uri *su = NULL;
	size_t i;
	int err = 0;

	for (i=0; i<RE_ARRAY_SIZE(testv); i++) {

		const struct test *test = &testv[i];
		struct pl pl;

		pl_set_str(&pl, test->uri);

		err = stunuri_decode(&su, &pl);
		if (err)
			break;

		ASSERT_EQ(test->scheme, su->scheme);
		ASSERT_STREQ(test->host, su->host);
		ASSERT_EQ(test->port, su->port);
		ASSERT_EQ(test->proto, su->proto);

		su = mem_deref(su);
	}

 out:
	mem_deref(su);

	return err;
}
