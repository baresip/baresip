/**
 * @file test/message.c  Baresip selftest -- message sending
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


struct test {
	enum sip_transp transp;
	int err;
};

struct endpoint {
	struct test *test;
	struct endpoint *other;
	struct message *message;
	struct ua *ua;
	char uri[256];
	unsigned n_msg;
	unsigned n_resp;
};


static const char dummy_msg[] = "hei paa deg";
static const char text_plain[] = "text/plain";


static bool endpoint_is_complete(const struct endpoint *ep)
{
	return ep->n_msg >= 1 || ep->n_resp >= 1;
}


static bool test_is_complete(struct endpoint *ep)
{
	return endpoint_is_complete(ep) &&
		endpoint_is_complete(ep->other);
}


static void message_recv_handler(struct ua *ua, const struct pl *peer,
				 const struct pl *ctype, struct mbuf *body,
				 void *arg)
{
	struct endpoint *ep = arg;
	int err = 0;
	(void)ua;

	info("[ %s ] recv msg from %r: \"%b\"\n", ep->uri, peer,
	     mbuf_buf(body), mbuf_get_left(body));

	TEST_STRCMP(text_plain, strlen(text_plain),
		    ctype->p, ctype->l);

	TEST_STRCMP(dummy_msg, str_len(dummy_msg),
		    mbuf_buf(body), mbuf_get_left(body));

	++ep->n_msg;

	if (test_is_complete(ep)) {
		re_cancel();
		return;
	}

 out:
	if (err) {
		ep->test->err = err;
		re_cancel();
	}
}


static void send_resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	struct endpoint *ep = arg;

	++ep->n_resp;

	if (err) {
		warning("sending failed: %m\n", err);
		goto out;
	}

	if (msg->scode >= 300) {
		warning("sending failed: %u %r\n", msg->scode, &msg->reason);
		err = EPROTO;
		goto out;
	}

	info("[ %s ] message sent OK\n", ep->uri);

	ASSERT_EQ(ep->test->transp, msg->tp);
	ASSERT_EQ(200, msg->scode);

	if (test_is_complete(ep)) {
		re_cancel();
		return;
	}

 out:
	if (err) {
		ep->test->err = err;
		re_cancel();
	}
}


static void endpoint_destructor(void *data)
{
	struct endpoint *ep = data;

	mem_deref(ep->message);
	mem_deref(ep->ua);
}


static int endpoint_alloc(struct endpoint **epp, struct test *test,
			  const char *name, enum sip_transp transp)
{
	struct endpoint *ep = NULL;
	struct sa laddr;
	char aor[256];
	int err = 0;

	err = sip_transp_laddr(uag_sip(), &laddr, transp, NULL);
	TEST_ERR(err);

	ep = mem_zalloc(sizeof(*ep), endpoint_destructor);
	if (!ep)
		return ENOMEM;

	ep->test = test;

	if (re_snprintf(aor, sizeof(aor),
			"%s <sip:%s@127.0.0.1;transport=%s>;regint=0",
			name, name,
			sip_transp_name(transp)) < 0) {
		err = ENOMEM;
		goto out;
	}

	if (re_snprintf(ep->uri, sizeof(ep->uri), "sip:%s@%J;transport=%s",
			name,
			&laddr, sip_transp_name(transp)) < 0) {
		err = ENOMEM;
		goto out;
	}

	err = ua_alloc(&ep->ua, aor);
	if (err)
		goto out;

	err = message_init(&ep->message);
	TEST_ERR(err);

 out:
	if (err)
		mem_deref(ep);
	else
		*epp = ep;

	return err;
}


static int test_message_transp(enum sip_transp transp)
{
	struct test test;
	struct endpoint *a = NULL, *b = NULL;
	bool enable_udp, enable_tcp;
	int err = 0;

	enable_udp = transp == SIP_TRANSP_UDP;
	enable_tcp = transp == SIP_TRANSP_TCP;

	memset(&test, 0, sizeof(test));

	test.transp = transp;

	err = ua_init("test", enable_udp, enable_tcp, false);
	TEST_ERR(err);

	err = endpoint_alloc(&a, &test, "a", transp);
	TEST_ERR(err);

	err = endpoint_alloc(&b, &test, "b", transp);
	TEST_ERR(err);

	a->other = b;
	b->other = a;

	/* NOTE: can only listen to one global instance for now */
	err = message_listen(b->message, message_recv_handler, b);
	TEST_ERR(err);

	/* Send a message from A to B */
	err = message_send(a->ua, b->uri, dummy_msg, send_resp_handler, a);
	TEST_ERR(err);

	err = re_main_timeout(1000);
	TEST_ERR(err);

	TEST_ERR(test.err);
	ASSERT_EQ(0, a->n_msg);
	ASSERT_EQ(1, a->n_resp);
	ASSERT_EQ(1, b->n_msg);
	ASSERT_EQ(0, b->n_resp);

 out:
	mem_deref(b);
	mem_deref(a);
	ua_close();

	return err;
}


int test_message(void)
{
	int err = 0;

	err = test_message_transp(SIP_TRANSP_UDP);
	TEST_ERR(err);

	err |= test_message_transp(SIP_TRANSP_TCP);
	TEST_ERR(err);

 out:
	return err;
}
