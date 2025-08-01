/**
 * @file test/account.c  Tests for account
 *
 * Copyright (C) 2010 - 2017 Alfred E. Heggestad
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


#define DEBUG_MODULE "account"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


static const char str[] =
	"\"Mr User\" <sip:user@domain.com>"
	";answermode=auto"
	";auth_user=xuser"
	";auth_pass=pass"
	";outbound=\"sip:edge.domain.com\""
	";ptime=10"
	";regint=600"
	";pubint=700"
	";sipnat=outbound"
	";stunuser=bob@bob.com"
	";stunpass=taj:aa"
	";stunserver=\"stun:stunserver.org\""
	";mwi=no"
	";call_transfer=no"
	";audio_source=null,null"
	";video_source=null,null"
	;


int test_account(void)
{
	struct account *acc = NULL;
	struct sip_addr *addr;
	int err = 0;

	err = account_alloc(&acc, str);
	TEST_ERR(err);
	ASSERT_TRUE(acc != NULL);

	/* verify the decoded SIP aor */
	addr = account_laddr(acc);
	ASSERT_TRUE(addr != NULL);
	TEST_STRCMP("Mr User", 7, addr->dname.p,        addr->dname.l);
	TEST_STRCMP("sip",     3, addr->uri.scheme.p,   addr->uri.scheme.l);
	TEST_STRCMP("user",    4, addr->uri.user.p,     addr->uri.user.l);
	TEST_STRCMP("domain.com", 10, addr->uri.host.p, addr->uri.host.l);
	ASSERT_EQ(0, addr->uri.params.l);
	ASSERT_TRUE(addr->params.l > 0);

	/* verify all decoded parameters */
	ASSERT_STREQ("Mr User", account_display_name(acc));
	ASSERT_TRUE(ANSWERMODE_AUTO == account_answermode(acc));
	ASSERT_STREQ("xuser", account_auth_user(acc));
	ASSERT_STREQ("pass", account_auth_pass(acc));
	ASSERT_STREQ("sip:edge.domain.com", account_outbound(acc, 0));
	ASSERT_TRUE(NULL == account_outbound(acc, 1));
	ASSERT_TRUE(NULL == account_outbound(acc, 333));
	ASSERT_EQ(10, account_ptime(acc));
	ASSERT_EQ(600, account_regint(acc));
	ASSERT_EQ(700, account_pubint(acc));
	ASSERT_STREQ("bob@bob.com", account_stun_user(acc));
	ASSERT_STREQ("taj:aa", account_stun_pass(acc));
	ASSERT_STREQ("stunserver.org", account_stun_host(acc));
	ASSERT_TRUE(!account_mwi(acc));
	ASSERT_TRUE(!account_call_transfer(acc));

 out:
	mem_deref(acc);
	return err;
}


int test_account_uri_complete(void)
{
	static const struct test {
		const char *in;
		const char *out;
	} testv[] = {

		{ "192.168.1.2",
		  "sip:192.168.1.2" },

		{ "192.168.1.2:5677",
		  "sip:192.168.1.2:5677", },

		{ "user",
		  "sip:user@proxy.com" },

		{ "user@domain.com",
		  "sip:user@domain.com" },

		{ "user@domain.com:5677",
		  "sip:user@domain.com:5677" },

		{ "sip:office.local",
		  "sip:office.local" },

		{ "sip:user@domain.com",
		  "sip:user@domain.com" },

		{"[2113:1470:1f1b:24b::2]",
		 "sip:[2113:1470:1f1b:24b::2]"},

		{"[fe80::b62e:99ff:feee:268f]",
		 "sip:[fe80::b62e:99ff:feee:268f]"},

		{"x@[2113:1470:1f1b:24b::2]",
		 "sip:x@[2113:1470:1f1b:24b::2]"},

		{"[2113:1470:1f1b:24b::2]:5677",
		 "sip:[2113:1470:1f1b:24b::2]:5677"},

		{"x@[2113:1470:1f1b:24b::2]:5677",
		 "sip:x@[2113:1470:1f1b:24b::2]:5677"},
	};

	struct mbuf *mb = NULL;
	struct account *acc = NULL;
	int err = 0;

	err = account_alloc(&acc, "\"A\" <sip:A@proxy.com>");
	TEST_ERR(err);

	mb = mbuf_alloc(256);
	ASSERT_TRUE(mb != NULL);

	for (size_t i=0; i<RE_ARRAY_SIZE(testv); i++) {

		const struct test *test = &testv[i];

		err = account_uri_complete(acc, mb, test->in);
		TEST_ERR_TXT(err, test->in);

		TEST_STRCMP(test->out, str_len(test->out), mb->buf, mb->end);

		mbuf_rewind(mb);
	}

 out:
	mem_deref(mb);
	mem_deref(acc);

	return err;
}
