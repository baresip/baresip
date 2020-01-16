/**
 * @file test/account.c  Tests for account
 *
 * Copyright (C) 2010 - 2017 Creytiv.com
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
	";audio_codecs=RAW-CODEC"
	;


int test_account(void)
{
	struct account *acc = NULL;
	struct sip_addr *addr;
	const struct aucodec *ac;
	struct list *lst;
	int err = 0;

	mock_aucodec_register(baresip_aucodecl());

	err = account_alloc(&acc, str);
	TEST_ERR(err);
	ASSERT_TRUE(acc != NULL);

	/* verify the decoded SIP aor */
	addr = account_laddr(acc);
	ASSERT_TRUE(addr != NULL);
	TEST_STRCMP("Mr User", 7, addr->dname.p,        addr->dname.l);
	TEST_STRCMP("sip",     3, addr->uri.scheme.p,   addr->uri.scheme.l);
	TEST_STRCMP("user",    4, addr->uri.user.p,     addr->uri.user.l);
	TEST_STRCMP("",        0, addr->uri.password.p, addr->uri.password.l);
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
	ASSERT_STREQ("no", account_mwi(acc));
	ASSERT_STREQ("no", account_call_transfer(acc));

	lst = account_aucodecl(acc);
	ac = list_ledata(list_head(lst));
	ASSERT_EQ(1, list_count(lst));
	ASSERT_STREQ("RAW-CODEC", ac->name);

 out:
	mock_aucodec_unregister();

	mem_deref(acc);
	return err;
}
