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
	""
	";100rel=yes"
	";answerdelay=1000"
	";answermode=auto"
	";audio_codecs=pcmu/8000/1,pcma"
	";audio_source=null,null"
	";autelev_pt=101"
	";auth_pass=pass"
	";auth_user=xuser"
	";call_transfer=no"
	";catchall=yes"
	";dtmfmode=auto"
	";extra=EXTRA"
	";fbregint=120"
	";inreq_allowed=yes"
	";mwi=no"
	";natpinhole=yes"
	";outbound=\"sip:edge.domain.com\""
	";prio=42"
	";ptime=10"
	";pubint=700"
	";regint=600"
	";regq=0.5"
	";rtcp_mux=yes"
	";rwait=3600"
	";sip_autoanswer=yes"
	";sip_autoanswer_beep=yes"
	";sip_autoredirect=no"
	";sipnat=outbound"
	";stunpass=taj:aa"
	";stunserver=\"stun:stunserver.org\""
	";stunuser=bob@bob.com"
	";tcpsrcport=49152"
	";video_codecs=h266"
	";video_display=sdl,default"
	";video_source=null,null"
	;


int test_account(void)
{
	struct account *acc = NULL;
	struct sip_addr *addr;
	struct odict *od = NULL;
	struct odict *odcfg = NULL;
	int err = 0;

	err = module_load(".", "g711");
	TEST_ERR(err);
	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);
	err = module_load(".", "ice");
	TEST_ERR(err);

	mock_vidcodec_register();

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
	ASSERT_STREQ("outbound", account_sipnat(acc));
	ASSERT_STREQ("EXTRA", account_extra(acc));

	err = account_set_auth_user(acc, "AUTH-USER");
	TEST_ERR(err);
	err = account_set_auth_pass(acc, "AUTH-PASS");
	TEST_ERR(err);
	err = account_set_outbound(acc, "outbound.example.com", 1);
	TEST_ERR(err);
	err = account_set_regint(acc, 60);
	TEST_ERR(err);
	err = account_set_stun_uri(acc, "stun:stun.example.com");
	TEST_ERR(err);
	err = account_set_stun_host(acc, "stun.example.com");
	TEST_ERR(err);
	err = account_set_stun_port(acc, 19302);
	TEST_ERR(err);
	err = account_set_stun_user(acc, "STUN-USER");
	TEST_ERR(err);
	err = account_set_stun_pass(acc, "STUN-PASS");
	TEST_ERR(err);
	err = account_set_ausrc_dev(acc, "default");
	TEST_ERR(err);
	err = account_set_auplay_dev(acc, "default");
	TEST_ERR(err);
	err = account_set_mediaenc(acc, "dtls_srtp");
	TEST_ERR(err);
	err = account_set_medianat(acc, "ice");
	TEST_ERR(err);
	err = account_set_audio_codecs(acc, "pcmu");
	TEST_ERR(err);
	err = account_set_video_codecs(acc, "h266");
	TEST_ERR(err);
	err = account_set_mwi(acc, false);
	TEST_ERR(err);
	err = account_set_call_transfer(acc, false);
	TEST_ERR(err);
	err = account_set_rtcp_mux(acc, true);
	TEST_ERR(err);
	account_set_catchall(acc, true);
	err = account_set_pubint(acc, 3600);
	TEST_ERR(err);

	ASSERT_EQ(120, account_fbregint(acc));
	ASSERT_EQ(19302, account_stun_port(acc));

	err = account_set_display_name(acc, "Display");
	TEST_ERR(err);

	err = account_set_answermode(acc, ANSWERMODE_MANUAL);
	TEST_ERR(err);

	err = account_set_rel100_mode(acc, REL100_REQUIRED);
	TEST_ERR(err);

	err = account_set_dtmfmode(acc, DTMFMODE_RTP_EVENT);
	TEST_ERR(err);

	account_set_answerdelay(acc, 1000);

	account_set_autelev_pt(acc, 101);

	ASSERT_EQ(1000, account_answerdelay(acc));
	ASSERT_EQ(101, account_autelev_pt(acc));

	ASSERT_TRUE(account_rtcp_mux(acc));

	err = account_set_inreq_mode(acc, INREQ_MODE_ON);
	TEST_ERR(err);

	enum { HASH_SIZE = 32 };
	err |= odict_alloc(&od, HASH_SIZE);
	err |= odict_alloc(&odcfg, HASH_SIZE);
	TEST_ERR(err);

	err = account_json_api(od, odcfg, acc);
	TEST_ERR(err);

	re_printf("%H\n", account_debug, acc);
	re_printf("%H\n", odict_debug, od);
	re_printf("%H\n", odict_debug, odcfg);

 out:
	mock_vidcodec_unregister();

	module_unload("ice");
	module_unload("dtls_srtp");
	module_unload("g711");

	mem_deref(acc);
	mem_deref(odcfg);
	mem_deref(od);

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
