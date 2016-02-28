/**
 * @file test/call.c  Baresip selftest -- call
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


#define MAGIC 0x7004ca11


enum behaviour {
	BEHAVIOUR_ANSWER = 0,
	BEHAVIOUR_REJECT
};

enum action {
	ACTION_RECANCEL = 0,
	ACTION_HANGUP_A,
	ACTION_HANGUP_B
};

struct agent {
	struct agent *peer;
	struct ua *ua;
	uint16_t close_scode;
	bool failed;

	unsigned n_incoming;
	unsigned n_established;
	unsigned n_closed;
};

struct fixture {
	uint32_t magic;
	struct agent a, b;
	struct sa laddr_sip;
	enum behaviour behaviour;
	enum action estab_action;
	char buri[256];
	int err;
};


#define fixture_init(f)							\
	memset(f, 0, sizeof(*f));					\
									\
	f->magic = MAGIC;						\
	aucodec_register(&dummy_pcma);					\
									\
	err = ua_alloc(&f->a.ua, "A <sip:a:xxx@127.0.0.1>;regint=0");	\
	TEST_ERR(err);							\
	err = ua_alloc(&f->b.ua, "B <sip:b:xxx@127.0.0.1>;regint=0");	\
	TEST_ERR(err);							\
									\
	f->a.peer = &f->b;						\
	f->b.peer = &f->a;						\
									\
	err = uag_event_register(event_handler, f);			\
	TEST_ERR(err);							\
									\
	err = sip_transp_laddr(uag_sip(), &f->laddr_sip,		\
			       SIP_TRANSP_UDP, NULL);			\
	TEST_ERR(err);							\
									\
	re_snprintf(f->buri, sizeof(f->buri), "sip:b@%J", &f->laddr_sip);


#define fixture_close(f)			\
	mem_deref(f->b.ua);			\
	mem_deref(f->a.ua);			\
						\
	aucodec_unregister(&dummy_pcma);	\
						\
	uag_event_unregister(event_handler)


static struct aucodec dummy_pcma = {
	.pt = "8",
	.name = "PCMA",
	.srate = 8000,
	.ch = 1,
};


static void event_handler(struct ua *ua, enum ua_event ev,
			  struct call *call, const char *prm, void *arg)
{
	struct fixture *f = arg;
	struct agent *ag;
	int err = 0;
	(void)prm;

#if 0
	re_printf("[ %s ] event: %s (%s)\n",
		  ua_aor(ua), uag_event_str(ev), prm);
#endif

	ASSERT_TRUE(f != NULL);
	ASSERT_EQ(MAGIC, f->magic);

	if (ua == f->a.ua)
		ag = &f->a;
	else if (ua == f->b.ua)
		ag = &f->b;
	else {
		return;
	}

	switch (ev) {

	case UA_EVENT_CALL_INCOMING:
		++ag->n_incoming;

		switch (f->behaviour) {

		case BEHAVIOUR_ANSWER:
			err = ua_answer(ua, call);
			if (err) {
				warning("ua_answer failed (%m)\n", err);
				goto out;
			}
			break;

		case BEHAVIOUR_REJECT:
			ua_hangup(ua, call, 0, 0);
			call = NULL;
			ag->failed = true;
			break;

		default:
			break;
		}
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		++ag->n_established;

		/* are both agents established? */
		if (ag->peer->n_established) {

			switch (f->estab_action) {

			case ACTION_RECANCEL:
				re_cancel();
				break;

			case ACTION_HANGUP_A:
				f->a.failed = true;
				ua_hangup(f->a.ua, NULL, 0, 0);
				break;

			case ACTION_HANGUP_B:
				f->b.failed = true;
				ua_hangup(f->b.ua, NULL, 0, 0);
				break;
			}
		}
		break;

	case UA_EVENT_CALL_CLOSED:
		ag->failed = true;
		++ag->n_closed;

		ag->close_scode = call_scode(call);

		if (ag->peer->n_closed) {
			re_cancel();
		}
		break;

	default:
		break;
	}

	if (ag->failed && ag->peer->failed) {
		re_cancel();
		return;
	}

 out:
	if (err) {
		warning("error in event-handler (%m)\n", err);
		f->err = err;
		re_cancel();
	}
}


int test_call_answer(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(0, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(0, fix.b.n_closed);

 out:
	fixture_close(f);

	return err;
}


int test_call_reject(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_REJECT;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);

 out:
	fixture_close(f);

	return err;
}


int test_call_af_mismatch(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	ua_set_media_af(f->a.ua, AF_INET6);
	ua_set_media_af(f->b.ua, AF_INET);

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(488, fix.a.close_scode);

	ASSERT_EQ(0, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);

 out:
	fixture_close(f);

	return err;
}


int test_call_answer_hangup_a(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_HANGUP_A;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(0, fix.b.close_scode);

 out:
	fixture_close(f);

	return err;
}


int test_call_answer_hangup_b(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_HANGUP_B;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(0, fix.b.close_scode);

 out:
	fixture_close(f);

	return err;
}
