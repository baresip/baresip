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
	ACTION_HANGUP_B,
	ACTION_NOTHING
};

struct agent {
	struct fixture *fix;    /* pointer to parent */
	struct agent *peer;
	struct ua *ua;
	uint16_t close_scode;
	bool failed;

	unsigned n_incoming;
	unsigned n_established;
	unsigned n_closed;
	unsigned n_dtmf_recv;
};

struct fixture {
	uint32_t magic;
	struct agent a, b;
	struct sa laddr_sip;
	enum behaviour behaviour;
	enum action estab_action;
	char buri[256];
	int err;
	unsigned exp_estab;
	unsigned exp_closed;
};


#define fixture_init_prm(f, prm)					\
	memset(f, 0, sizeof(*f));					\
									\
	f->a.fix = f;							\
	f->b.fix = f;							\
									\
	err = ua_init("test", true, true, true, false);			\
	TEST_ERR(err);							\
									\
	f->magic = MAGIC;						\
	f->exp_estab = 1;						\
	f->exp_closed = 1;						\
	aucodec_register(&dummy_pcma);					\
									\
	err = ua_alloc(&f->a.ua,					\
		       "A <sip:a:xxx@127.0.0.1>;regint=0" prm);		\
	TEST_ERR(err);							\
	err = ua_alloc(&f->b.ua,					\
		       "B <sip:b:xxx@127.0.0.1>;regint=0" prm);		\
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


#define fixture_init(f)				\
	fixture_init_prm((f), "");


#define fixture_close(f)			\
	mem_deref(f->b.ua);			\
	mem_deref(f->a.ua);			\
						\
	aucodec_unregister(&dummy_pcma);	\
						\
	uag_event_unregister(event_handler);	\
						\
	ua_stop_all(true);			\
	ua_close();

#define fixture_abort(f, error)			\
	do {					\
		(f)->err = (error);		\
		re_cancel();			\
	} while (0)


static struct aucodec dummy_pcma = {
	.pt = "8",
	.name = "PCMA",
	.srate = 8000,
	.crate = 8000,
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
		if (ag->n_established >= f->exp_estab &&
		    ag->peer->n_established >= f->exp_estab) {

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

			case ACTION_NOTHING:
				/* Do nothing, wait */
				break;
			}
		}
		break;

	case UA_EVENT_CALL_CLOSED:
		++ag->n_closed;

		ag->close_scode = call_scode(call);

		if (ag->close_scode)
			ag->failed = true;

		if (ag->n_closed >= f->exp_closed &&
		    ag->peer->n_closed >= f->exp_closed) {

			re_cancel();
		}
		break;

	default:
		break;
	}

	if (ag->failed && ag->peer->failed) {
		info("test: re_cancel on call failed\n");
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


int test_call_rtp_timeout(void)
{
#define RTP_TIMEOUT_MS 1
	struct fixture fix, *f = &fix;
	struct call *call;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
	TEST_ERR(err);

	call = ua_call(f->a.ua);
	ASSERT_TRUE(call != NULL);

	call_enable_rtp_timeout(call, RTP_TIMEOUT_MS);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(701, fix.a.close_scode);  /* verify timeout */

	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(0, fix.b.close_scode);

 out:
	fixture_close(f);

	return err;
}


/* veriy that line-numbers are in sequence */
static bool linenum_are_sequential(const struct ua *ua)
{
	uint32_t linenum = 0;
	struct le *le;

	for (le = list_head(ua_calls(ua)) ; le ; le = le->next) {
		struct call *call = le->data;

		if (call_linenum(call) <= linenum)
			return false;

		linenum = call_linenum(call);
	}

	return true;
}


int test_call_multiple(void)
{
	struct fixture fix, *f = &fix;
	struct le *le;
	unsigned i;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->exp_estab = 4;

	/*
	 * Step 1 -- make 4 calls from A to B
	 */
	for (i=0; i<4; i++) {
		err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
		TEST_ERR(err);
	}

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(4, fix.a.n_established);
	ASSERT_EQ(0, fix.a.n_closed);

	ASSERT_EQ(4, fix.b.n_incoming);
	ASSERT_EQ(4, fix.b.n_established);
	ASSERT_EQ(0, fix.b.n_closed);

	ASSERT_EQ(4, list_count(ua_calls(f->a.ua)));
	ASSERT_EQ(4, list_count(ua_calls(f->b.ua)));
	ASSERT_TRUE(linenum_are_sequential(f->a.ua));
	ASSERT_TRUE(linenum_are_sequential(f->b.ua));


	/*
	 * Step 2 -- hangup calls with even line-number
	 */

	f->exp_closed = 2;

	le = list_head(ua_calls(f->a.ua));
	while (le) {
		struct call *call = le->data;
		le = le->next;

		if (!(call_linenum(call) % 2)) {
			ua_hangup(f->a.ua, call, 0, 0);
		}
	}

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(2, list_count(ua_calls(f->a.ua)));
	ASSERT_EQ(2, list_count(ua_calls(f->b.ua)));
	ASSERT_TRUE(linenum_are_sequential(f->a.ua));
	ASSERT_TRUE(linenum_are_sequential(f->b.ua));


	/*
	 * Step 3 -- make 2 calls from A to B
	 */

	f->a.n_established = 0;
	f->b.n_established = 0;
	f->exp_estab = 2;
	for (i=0; i<2; i++) {
		err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
		TEST_ERR(err);
	}

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(4, list_count(ua_calls(f->a.ua)));
	ASSERT_EQ(4, list_count(ua_calls(f->b.ua)));

 out:
	fixture_close(f);

	return err;
}


int test_call_max(void)
{
	struct fixture fix, *f = &fix;
	unsigned i;
	int err = 0;

	/* Set the max-calls limit */
	conf_config()->call.max_calls = 1;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make 2 calls, one should work and one should fail */
	for (i=0; i<2; i++) {
		err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
		TEST_ERR(err);
	}

	f->b.failed = true; /* tiny hack to stop the runloop */

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(486, fix.a.close_scode);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_closed);

 out:
	fixture_close(f);

	return err;
}


static const char dtmf_digits[] = "123";


static void dtmf_handler(struct call *call, char key, void *arg)
{
	struct agent *ag = arg;
	int err = 0;
	(void)call;

	/* ignore key-release */
	if (key == KEYCODE_REL)
		return;

	ASSERT_EQ(dtmf_digits[ag->n_dtmf_recv], key);
	++ag->n_dtmf_recv;

	if (ag->n_dtmf_recv >= str_len(dtmf_digits)) {
		re_cancel();
	}

 out:
	if (err) {
		fixture_abort(ag->fix, err);
	}
}


int test_call_dtmf(void)
{
	struct fixture fix, *f = &fix;
	struct ausrc *ausrc = NULL;
	size_t i, n = str_len(dtmf_digits);
	int err = 0;

	/* Use a low packet time, so the test completes quickly */
	fixture_init_prm(f, ";ptime=1");

	/* audio-source is needed for dtmf/telev to work */
	err = mock_ausrc_register(&ausrc);
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, NULL, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	call_set_handlers(ua_call(f->a.ua), NULL, dtmf_handler, &f->a);
	call_set_handlers(ua_call(f->b.ua), NULL, dtmf_handler, &f->b);

	/* send some DTMF digits from A to B .. */
	for (i=0; i<n; i++) {
		err  = call_send_digit(ua_call(f->a.ua), dtmf_digits[i]);
		TEST_ERR(err);
	}

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_dtmf_recv);
	ASSERT_EQ(n, fix.b.n_dtmf_recv);

 out:
	fixture_close(f);
	mem_deref(ausrc);

	return err;
}
