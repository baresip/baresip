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
	} testv[] = {
		{
			"stun:example.org",
			STUN_SCHEME_STUN, "example.org", 0
		},
		{
			"stuns:example.org",
			STUN_SCHEME_STUNS, "example.org", 0
		},
		{
			"stun:example.org:8000",
			STUN_SCHEME_STUN, "example.org", 8000
		},
		{
			"turn:example.org",
			STUN_SCHEME_TURN, "example.org", 0
		},
		{
			"turns:example.org",
			STUN_SCHEME_TURNS, "example.org", 0
		},
		{
			"turn:example.org:8000",
			STUN_SCHEME_TURN, "example.org", 8000
		}
	};
	struct stun_uri *su = NULL;
	size_t i;
	int err = 0;

	for (i=0; i<ARRAY_SIZE(testv); i++) {

		const struct test *test = &testv[i];
		struct pl pl;

		pl_set_str(&pl, test->uri);

		err = stunuri_decode(&su, &pl);
		if (err)
			break;

		ASSERT_EQ(test->scheme, su->scheme);
		ASSERT_STREQ(test->host, su->host);
		ASSERT_EQ(test->port, su->port);

		su = mem_deref(su);
	}

 out:
	mem_deref(su);

	return err;
}
