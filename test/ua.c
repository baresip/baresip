/**
 * @file test/ua.c  Baresip selftest -- User-Agent (UA)
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"
#include "sip/sipsrv.h"


#define MAGIC 0x9044bbfc


struct test {
	struct sip_server *srvv[16];
	size_t srvc;
	struct ua *ua;
	int err;
	unsigned got_register_ok;
	unsigned n_resp;
	uint32_t magic;
	enum sip_transp tp_resp;
};


static void test_init(struct test *t)
{
	memset(t, 0, sizeof(*t));
	t->magic = MAGIC;
}


static void test_reset(struct test *t)
{
	size_t i;

	for (i=0; i<ARRAY_SIZE(t->srvv); i++)
		mem_deref(t->srvv[i]);
	mem_deref(t->ua);

	memset(t, 0, sizeof(*t));
}


static void test_abort(struct test *t, int err)
{
	t->err = err;
	re_cancel();
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct test *t = arg;
	size_t i;
	int err = 0;
	(void)call;
	(void)prm;

	ASSERT_TRUE(t != NULL);

	if (ua != t->ua)
		return;

	if (ev == UA_EVENT_REGISTER_OK) {

		info("event: Register OK!\n");

		++t->got_register_ok;

		/* verify register success */
		ASSERT_TRUE(ua_isregistered(t->ua));

		/* Terminate SIP Server, then De-REGISTER */
		for (i=0; i<t->srvc; i++)
			t->srvv[i]->terminate = true;

		t->ua = mem_deref(t->ua);
	}
	else if (ev == UA_EVENT_REGISTER_FAIL) {

		err = EAUTH;
		re_cancel();
	}

 out:
	if (err) {
		warning("selftest: event handler error: %m\n", err);
		t->err = err;
	}
}


static void sip_server_exit_handler(void *arg)
{
	(void)arg;
	re_cancel();
}


static int reg(enum sip_transp tp)
{
	struct test t;
	char aor[256];
	int err;

	memset(&t, 0, sizeof(t));

	err = sip_server_alloc(&t.srvv[0], sip_server_exit_handler, NULL);
	if (err) {
		warning("failed to create sip server (%d/%m)\n", err, err);
		goto out;
	}

	t.srvc = 1;

	err = sip_server_uri(t.srvv[0], aor, sizeof(aor), tp);
	TEST_ERR(err);

	err = ua_alloc(&t.ua, aor);
	TEST_ERR(err);

	err = ua_register(t.ua);
	TEST_ERR(err);

	err = uag_event_register(ua_event_handler, &t);
	if (err)
		goto out;

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	if (err)
		goto out;

	if (t.err)
		err = t.err;

	ASSERT_TRUE(t.srvv[0]->n_register_req > 0);
	ASSERT_EQ(tp, t.srvv[0]->tp_last);
	ASSERT_TRUE(t.got_register_ok > 0);

 out:
	if (err) {
		warning("selftest: ua_register test failed (%m)\n", err);
	}
	uag_event_unregister(ua_event_handler);
	test_reset(&t);

	return err;
}


int test_ua_register(void)
{
	int err = 0;

	err = ua_init("test", true, true, true);
	TEST_ERR(err);

	err |= reg(SIP_TRANSP_UDP);
	err |= reg(SIP_TRANSP_TCP);
#ifdef USE_TLS
	err |= reg(SIP_TRANSP_TLS);
#endif

	ua_stop_all(true);
	ua_close();

 out:
	return err;
}


int test_ua_alloc(void)
{
	struct ua *ua;
	struct mbuf *mb = mbuf_alloc(512);
	uint32_t n_uas = list_count(uag_list());
	int err = 0;

	/* make sure we dont have that UA already */
	ASSERT_TRUE(NULL == uag_find_aor("sip:user@127.0.0.1"));

	err = ua_alloc(&ua, "Foo <sip:user@127.0.0.1>;regint=0");
	if (err)
		return err;

	/* verify this UA-instance */
	ASSERT_TRUE(!ua_isregistered(ua));
	ASSERT_STREQ("sip:user@127.0.0.1", ua_aor(ua));
	ASSERT_TRUE(NULL == ua_call(ua));

	/* verify global UA keeper */
	ASSERT_EQ((n_uas + 1), list_count(uag_list()));
	ASSERT_TRUE(ua == uag_find_aor("sip:user@127.0.0.1"));

	/* verify URI complete function */
	err = ua_uri_complete(ua, mb, "bob");
	ASSERT_EQ(0, err);
	TEST_STRCMP("sip:bob@127.0.0.1", 17, mb->buf, mb->end);

	mem_deref(ua);

	ASSERT_EQ((n_uas), list_count(uag_list()));

 out:
	mem_deref(mb);
	return err;
}


int test_uag_find_param(void)
{
	struct ua *ua1 = NULL, *ua2 = NULL;
	int err = 0;

	ASSERT_TRUE(NULL == uag_find_param("not", "found"));

	err  = ua_alloc(&ua1, "<sip:x@127.0.0.1>;regint=0;abc");
	err |= ua_alloc(&ua2, "<sip:x@127.0.0.1>;regint=0;def=123");
	if (err)
		goto out;

	ASSERT_TRUE(ua1  == uag_find_param("abc", NULL));
	ASSERT_TRUE(NULL == uag_find_param("abc", "123"));
	ASSERT_TRUE(ua2  == uag_find_param("def", NULL));
	ASSERT_TRUE(ua2  == uag_find_param("def", "123"));

	ASSERT_TRUE(NULL == uag_find_param("not", "found"));

 out:
	mem_deref(ua2);
	mem_deref(ua1);

	return err;
}


static const char *_sip_transp_srvid(enum sip_transp tp)
{
	switch (tp) {

	case SIP_TRANSP_UDP: return "_sip._udp";
	case SIP_TRANSP_TCP: return "_sip._tcp";
	case SIP_TRANSP_TLS: return "_sips._tcp";
	default:             return "???";
	}
}


static int reg_dns(enum sip_transp tp)
{
	struct dns_server *dnssrv = NULL;
	struct test t;
	const char *domain = "test.invalid";
	struct network *net = baresip_network();
	unsigned server_count = 1;
	char aor[256];
	char srv[256];
	size_t i;
	int err;

	memset(&t, 0, sizeof(t));

	/*
	 * Setup server-side mocks:
	 */

	err = dns_server_alloc(&dnssrv, true);
	TEST_ERR(err);

	info("| DNS-server on %J\n", &dnssrv->addr);

	/* NOTE: must be done before ua_init() */
	err = net_use_nameserver(net, &dnssrv->addr, 1);
	TEST_ERR(err);

	for (i=0; i<server_count; i++) {
		struct sa sip_addr;
		char arec[256];

		err = sip_server_alloc(&t.srvv[i],
				       sip_server_exit_handler, NULL);
		if (err) {
			warning("failed to create sip server (%d/%m)\n",
				err, err);
			goto out;
		}

		err = domain_add(t.srvv[0], domain);
		if (err)
			goto out;

		err = sip_transp_laddr(t.srvv[i]->sip, &sip_addr, tp, NULL);
		TEST_ERR(err);

		info("| SIP-server on %J\n", &sip_addr);

		re_snprintf(arec, sizeof(arec),
			    "alpha%u.%s", i+1, domain);

		re_snprintf(srv, sizeof(srv),
			    "%s.%s", _sip_transp_srvid(tp), domain);
		err = dns_server_add_srv(dnssrv, srv,
					 20, 0, sa_port(&sip_addr),
					 arec);
		TEST_ERR(err);

		err = dns_server_add_a(dnssrv, arec, sa_in(&sip_addr));
		TEST_ERR(err);
	}
	t.srvc = server_count;

	/* NOTE: angel brackets needed to parse ;transport parameter */
	if (re_snprintf(aor, sizeof(aor), "<sip:x@%s;transport=%s>",
			domain, sip_transp_name(tp)) < 0)
		return ENOMEM;

	/*
	 * Start SIP client:
	 */

	err = ua_init("test", true, true, true);
	TEST_ERR(err);

	err = ua_alloc(&t.ua, aor);
	TEST_ERR(err);

	err = ua_register(t.ua);
	TEST_ERR(err);

	err = uag_event_register(ua_event_handler, &t);
	if (err)
		goto out;

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	if (err)
		goto out;

	if (t.err)
		err = t.err;

	/* verify that all SIP requests was sent to the first
	 * SIP-server.
	 */
	ASSERT_TRUE(t.srvv[0]->n_register_req > 0);
	ASSERT_EQ(tp, t.srvv[0]->tp_last);
	ASSERT_TRUE(t.got_register_ok > 0);

 out:
	if (err) {
		warning("selftest: ua_register test failed (%m)\n", err);
	}
	uag_event_unregister(ua_event_handler);

	test_reset(&t);

	ua_stop_all(true);
	ua_close();

	mem_deref(dnssrv);

	return err;
}


int test_ua_register_dns(void)
{
	int err = 0;

	err |= reg_dns(SIP_TRANSP_UDP);
	TEST_ERR(err);
	err |= reg_dns(SIP_TRANSP_TCP);
	TEST_ERR(err);
#ifdef USE_TLS
	err |= reg_dns(SIP_TRANSP_TLS);
	TEST_ERR(err);
#endif

 out:
	return err;
}


#define USER   "alfredh"
#define PASS   "pass@word"
#define DOMAIN "localhost"

static int reg_auth(enum sip_transp tp)
{
	struct sa laddr;
	struct test t;
	char aor[256];
	int err;

	memset(&t, 0, sizeof(t));

	err = sip_server_alloc(&t.srvv[0], sip_server_exit_handler, NULL);
	if (err) {
		warning("failed to create sip server (%d/%m)\n", err, err);
		goto out;
	}

	t.srvc = 1;

	err = domain_add(t.srvv[0], DOMAIN);
	TEST_ERR(err);

	err = user_add(domain_lookup(t.srvv[0], DOMAIN)->ht_usr,
		       USER, PASS, DOMAIN);
	TEST_ERR(err);

	t.srvv[0]->auth_enabled = true;

	err = sip_transp_laddr(t.srvv[0]->sip, &laddr, tp, NULL);
	if (err)
		return err;

	/* NOTE: angel brackets needed to parse ;transport parameter */
	if (re_snprintf(aor, sizeof(aor),
			"<sip:%s@%s>"
			";auth_pass=%s"
			";outbound=\"sip:%J;transport=%s\"",
			USER,
			DOMAIN,
			PASS,
			&laddr,
			sip_transp_name(tp)) < 0)
		return ENOMEM;

	err = ua_alloc(&t.ua, aor);
	TEST_ERR(err);

	err = ua_register(t.ua);
	TEST_ERR(err);

	err = uag_event_register(ua_event_handler, &t);
	if (err)
		goto out;

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	if (err)
		goto out;

	if (t.err) {
		err = t.err;
		goto out;
	}

	ASSERT_TRUE(t.srvv[0]->n_register_req > 0);
	ASSERT_EQ(tp, t.srvv[0]->tp_last);
	ASSERT_TRUE(t.got_register_ok > 0);

 out:
	if (err) {
		warning("selftest: ua_register test failed (%m)\n", err);
	}
	uag_event_unregister(ua_event_handler);
	test_reset(&t);

	return err;
}


int test_ua_register_auth(void)
{
	int err;

	err = ua_init("test", true, true, true);
	TEST_ERR(err);

	err |= reg_auth(SIP_TRANSP_UDP);
	TEST_ERR(err);
	err |= reg_auth(SIP_TRANSP_TCP);
	TEST_ERR(err);
#ifdef USE_TLS
	err |= reg_auth(SIP_TRANSP_TLS);
	TEST_ERR(err);
#endif

 out:
	ua_stop_all(true);
	ua_close();

	return err;
}


static int reg_auth_dns(enum sip_transp tp)
{
	struct network *net = baresip_network();
	struct dns_server *dnssrv = NULL;
	struct test t;
	const char *username = "alfredh";
	const char *password = "password";
	const char *domain = "test.invalid";
	unsigned server_count = 2;
	char aor[256];
	char srv[256];
	unsigned i;
	unsigned total_req = 0;
	int err;

	memset(&t, 0, sizeof(t));

	/*
	 * Setup server-side mocks:
	 */

	err = dns_server_alloc(&dnssrv, true);
	TEST_ERR(err);

	info("| DNS-server on %J\n", &dnssrv->addr);

	/* NOTE: must be done before ua_init() */
	err = net_use_nameserver(net, &dnssrv->addr, 1);
	TEST_ERR(err);

	for (i=0; i<server_count; i++) {
		struct sa sip_addr;
		char arec[256];

		err = sip_server_alloc(&t.srvv[i],
				       sip_server_exit_handler, NULL);
		if (err) {
			warning("failed to create sip server (%d/%m)\n",
				err, err);
			goto out;
		}

		t.srvv[i]->instance = i;

#if 1
		/* Comment this out to have different random secrets
		 * on each SIP-Server instance */
		t.srvv[i]->secret = 42;
#endif

		err = domain_add(t.srvv[i], domain);
		if (err)
			goto out;

		err = user_add(domain_lookup(t.srvv[i], domain)->ht_usr,
			       username, password, domain);
		TEST_ERR(err);

		t.srvv[i]->auth_enabled = true;

		err = sip_transp_laddr(t.srvv[i]->sip, &sip_addr, tp, NULL);
		TEST_ERR(err);

		info("| SIP-server on %J\n", &sip_addr);

		re_snprintf(arec, sizeof(arec),
			    "alpha%u.%s", i+1, domain);

		re_snprintf(srv, sizeof(srv),
			    "%s.%s", _sip_transp_srvid(tp), domain);
		err = dns_server_add_srv(dnssrv, srv,
					 20, 0, sa_port(&sip_addr),
					 arec);
		TEST_ERR(err);

		err = dns_server_add_a(dnssrv, arec, sa_in(&sip_addr));
		TEST_ERR(err);
	}
	t.srvc = server_count;

	/* NOTE: angel brackets needed to parse ;transport parameter */
	if (re_snprintf(aor, sizeof(aor),
			"<sip:%s@%s;transport=%s>;auth_pass=%s",
			username, domain, sip_transp_name(tp),
			password) < 0)
		return ENOMEM;

	/*
	 * Start SIP client:
	 */

	err = ua_init("test", true, true, true);
	TEST_ERR(err);

	err = ua_alloc(&t.ua, aor);
	TEST_ERR(err);

	err = ua_register(t.ua);
	TEST_ERR(err);

	err = uag_event_register(ua_event_handler, &t);
	if (err)
		goto out;

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	if (err)
		goto out;

	if (t.err) {
		err = t.err;
		goto out;
	}

	/* verify that all SIP requests was sent to the
	 * SIP-servers.
	 */
	for (i=0; i<server_count; i++) {

		total_req += t.srvv[i]->n_register_req;

		if (t.srvv[i]->n_register_req) {
			ASSERT_EQ(tp, t.srvv[i]->tp_last);
		}
	}
	ASSERT_TRUE(total_req >= 2);
	ASSERT_TRUE(t.got_register_ok > 0);

 out:
	if (err) {
		warning("selftest: ua_register test failed (%m)\n", err);
	}
	uag_event_unregister(ua_event_handler);

	test_reset(&t);

	ua_stop_all(true);
	ua_close();

	mem_deref(dnssrv);

	return err;
}


int test_ua_register_auth_dns(void)
{
	int err = 0;

	err |= reg_auth_dns(SIP_TRANSP_UDP);
	TEST_ERR(err);
	err |= reg_auth_dns(SIP_TRANSP_TCP);
	TEST_ERR(err);
#ifdef USE_TLS
	err |= reg_auth_dns(SIP_TRANSP_TLS);
	TEST_ERR(err);
#endif

 out:
	return err;
}


static void options_resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	struct test *t = arg;
	const struct sip_hdr *hdr;
	struct pl content;
	uint32_t clen;

	ASSERT_EQ(MAGIC, t->magic);

	if (err) {
		test_abort(t, err);
		return;
	}
	if (msg->scode != 200) {
		test_abort(t, EPROTO);
		return;
	}

	++t->n_resp;

	t->tp_resp = msg->tp;

	/* Verify SIP headers */

	ASSERT_TRUE(sip_msg_hdr_has_value(msg, SIP_HDR_ALLOW, "INVITE"));
	ASSERT_TRUE(sip_msg_hdr_has_value(msg, SIP_HDR_ALLOW, "ACK"));
	ASSERT_TRUE(sip_msg_hdr_has_value(msg, SIP_HDR_ALLOW, "BYE"));
	ASSERT_TRUE(sip_msg_hdr_has_value(msg, SIP_HDR_ALLOW, "CANCEL"));

	hdr = sip_msg_hdr(msg, SIP_HDR_CONTACT);
	ASSERT_TRUE(hdr != NULL);
	ASSERT_TRUE(hdr->val.l != 0);

	ASSERT_EQ(0, pl_strcasecmp(&msg->ctyp.type, "application"));
	ASSERT_EQ(0, pl_strcasecmp(&msg->ctyp.subtype, "sdp"));

	clen = pl_u32(&msg->clen);
	ASSERT_TRUE(clen > 0);

	/* Verify the SDP content */

	pl_set_mbuf(&content, msg->mb);

	ASSERT_EQ(0, re_regex(content.p, content.l, "v=0"));
	ASSERT_EQ(0, re_regex(content.p, content.l, "a=tool:baresip"));
	ASSERT_EQ(0, re_regex(content.p, content.l, "m=audio"));

 out:
	if (err)
		t->err = err;
	re_cancel();
}


static int test_ua_options_base(enum sip_transp transp)
{
	struct test t;
	struct sa laddr;
	char uri[256];
	int n, err = 0;

	test_init(&t);

	err = ua_init("test",
		      transp == SIP_TRANSP_UDP,
		      transp == SIP_TRANSP_TCP,
		      false);
	TEST_ERR(err);

	err = sip_transp_laddr(uag_sip(), &laddr, transp, NULL);
	TEST_ERR(err);

	err = ua_alloc(&t.ua, "Foo <sip:user@127.0.0.1>;regint=0");
	TEST_ERR(err);

	/* NOTE: no angle brackets in the Request URI */
	n = re_snprintf(uri, sizeof(uri),
			"sip:user@127.0.0.1:%u%s",
			sa_port(&laddr), sip_transp_param(transp));
	ASSERT_TRUE(n > 0);

	err = ua_options_send(t.ua, uri, options_resp_handler, &t);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	if (err)
		goto out;

	if (t.err) {
		err = t.err;
		goto out;
	}

	/* verify after test is complete */
	ASSERT_EQ(1, t.n_resp);
	ASSERT_EQ(transp, t.tp_resp);

 out:
	test_reset(&t);

	ua_stop_all(true);
	ua_close();

	return err;
}


int test_ua_options(void)
{
	int err = 0;

	err |= test_ua_options_base(SIP_TRANSP_UDP);
	TEST_ERR(err);
	err |= test_ua_options_base(SIP_TRANSP_TCP);
	TEST_ERR(err);

 out:
	return err;
}
