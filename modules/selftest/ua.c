/**
 * @file selftest/ua.c  Baresip selftest -- User-Agent (UA)
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "selftest.h"


struct test {
	struct sip_server *srv;
	struct ua *ua;
	int err;
	bool got_register_ok;
};


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct test *t = arg;
	int err = 0;
	(void)call;
	(void)prm;

	ASSERT_TRUE(t != NULL);

	if (ua != t->ua)
		return;

	if (ev == UA_EVENT_REGISTER_OK) {

		t->got_register_ok = true;

		/* verify register success */
		ASSERT_TRUE(ua_isregistered(t->ua));

		/* Terminate SIP Server, then De-REGISTER */
		t->srv->terminate = true;
		t->ua = mem_deref(t->ua);
	}

 out:
	if (err) {
		warning("selftest: event handler error: %m\n", err);
		t->err = err;
	}
}


int test_ua_register(void)
{
	struct test t;
	char aor[256];
	int err;

	memset(&t, 0, sizeof t);

	err = sip_server_create(&t.srv);
	if (err)
		goto out;

	re_snprintf(aor, sizeof(aor), "sip:x:x@%J", &t.srv->laddr);

	err = ua_alloc(&t.ua, aor);
	if (err)
		goto out;

	err = uag_event_register(ua_event_handler, &t);
	if (err)
		goto out;

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5);
	if (err)
		goto out;

	if (t.err)
		err = t.err;

	ASSERT_TRUE(t.srv->got_register_req);
	ASSERT_TRUE(t.got_register_ok);

 out:
	if (err) {
		warning("selftest: ua_register test failed (%m)\n", err);
	}
	uag_event_unregister(ua_event_handler);
	mem_deref(t.srv);
	mem_deref(t.ua);

	return err;
}


int test_ua_alloc(void)
{
	struct ua *ua;
	uint32_t n_uas = list_count(uag_list());
	int err = 0;

	/* make sure we dont have that UA already */
	ASSERT_TRUE(NULL == uag_find_aor("sip:user@127.0.0.1"));

	err = ua_alloc(&ua, "Foo <sip:user:pass@127.0.0.1>;regint=0");
	if (err)
		return err;

	/* verify this UA-instance */
	ASSERT_EQ(-1, ua_sipfd(ua));
	ASSERT_TRUE(!ua_isregistered(ua));
	ASSERT_STREQ("sip:user@127.0.0.1", ua_aor(ua));
	ASSERT_TRUE(NULL == ua_call(ua));

	/* verify global UA keeper */
	ASSERT_EQ((n_uas + 1), list_count(uag_list()));
	ASSERT_TRUE(ua == uag_find_aor("sip:user@127.0.0.1"));

	mem_deref(ua);

	ASSERT_EQ((n_uas), list_count(uag_list()));

 out:
	return err;
}


int test_uag_find_param(void)
{
	struct ua *ua1 = NULL, *ua2 = NULL;
	int err = 0;

	ASSERT_TRUE(NULL == uag_find_param("not", "found"));

	err  = ua_alloc(&ua1, "<sip:x:x@127.0.0.1>;regint=0;abc");
	err |= ua_alloc(&ua2, "<sip:x:x@127.0.0.1>;regint=0;def=123");
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
