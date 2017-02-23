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
	unsigned n_msg;
	unsigned n_resp;
	int err;
};

static const char dummy_msg[] = "hei paa deg";
static const char text_plain[] = "text/plain";


static bool is_complete(struct test *test)
{
	return test->n_msg >= 1 && test->n_resp >= 1;
}


static void message_recv_handler(const struct pl *peer, const struct pl *ctype,
				 struct mbuf *body, void *arg)
{
	struct test *test = arg;
	int err = 0;

	re_printf("recv msg from %r: \"%b\"\n", peer,
		  mbuf_buf(body), mbuf_get_left(body));

	TEST_STRCMP(text_plain, strlen(text_plain),
		    ctype->p, ctype->l);

	TEST_STRCMP(dummy_msg, str_len(dummy_msg),
		    mbuf_buf(body), mbuf_get_left(body));

	++test->n_msg;

	if (is_complete(test))
		re_cancel();

 out:
	test->err = err;
}


static void send_resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	struct test *test = arg;

	++test->n_resp;

	if (err) {
		(void)re_fprintf(stderr, " \x1b[31m%m\x1b[;m\n", err);
		goto out;
	}

	if (msg->scode >= 300) {
		(void)re_fprintf(stderr, " \x1b[31m%u %r\x1b[;m\n",
				 msg->scode, &msg->reason);
		err = EPROTO;
		goto out;
	}

	ASSERT_EQ(test->transp, msg->tp);
	ASSERT_EQ(200, msg->scode);

	re_printf("message sent OK\n");

	if (is_complete(test))
		re_cancel();

 out:
	test->err = err;
}


static int test_message_transp(enum sip_transp transp)
{
	struct test test;
	struct ua *a, *b;
	struct sa laddr;
	struct message *msga=0, *msgb=0;
	char aor_a[256], aor_b[256];
	char buri[256];
	bool enable_udp, enable_tcp;
	int err = 0;

	enable_udp = transp == SIP_TRANSP_UDP;
	enable_tcp = transp == SIP_TRANSP_TCP;

	memset(&test, 0, sizeof(test));

	test.transp = transp;

	err = ua_init("test", enable_udp, enable_tcp, false, false);
	TEST_ERR(err);

	re_snprintf(aor_a, sizeof(aor_a),
		    "A <sip:a:a@127.0.0.1;transport=%s>;regint=0",
		    sip_transp_name(transp));
	err |= ua_alloc(&a, aor_a);
	TEST_ERR(err);

	re_snprintf(aor_b, sizeof(aor_b),
		    "B <sip:b:b@127.0.0.1;transport=%s>;regint=0",
		    sip_transp_name(transp));
	err |= ua_alloc(&b, aor_b);
	TEST_ERR(err);

	err = sip_transp_laddr(uag_sip(), &laddr, transp, NULL);
	TEST_ERR(err);

	re_snprintf(buri, sizeof(buri), "sip:b@%J;transport=%s",
		    &laddr, sip_transp_name(transp));


	err = message_init(&msgb);
	TEST_ERR(err);

	err = message_listen(NULL, msgb, message_recv_handler, &test);
	TEST_ERR(err);

	err = message_send(a, buri, dummy_msg, send_resp_handler, &test);
	TEST_ERR(err);

	err = re_main_timeout(1000);
	TEST_ERR(err);

	TEST_ERR(test.err);
	ASSERT_EQ(1, test.n_msg);
	ASSERT_EQ(1, test.n_resp);

 out:
	mem_deref(msgb);
	mem_deref(msga);
	ua_close();

	return err;
}


int test_message(void)
{
	int err = 0;

	err |= test_message_transp(SIP_TRANSP_UDP);
	err |= test_message_transp(SIP_TRANSP_TCP);

	return err;
}
