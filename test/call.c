/**
 * @file test/call.c  Baresip selftest -- call
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "test.h"


#define MAGIC 0x7004ca11


enum behaviour {
	BEHAVIOUR_ANSWER = 0,
	BEHAVIOUR_PROGRESS,
	BEHAVIOUR_REJECT,
	BEHAVIOUR_GET_HDRS,
};

enum action {
	ACTION_RECANCEL = 0,
	ACTION_HANGUP_A,
	ACTION_HANGUP_B,
	ACTION_NOTHING,
	ACTION_TRANSFER
};

struct agent {
	struct fixture *fix;    /* pointer to parent */
	struct agent *peer;
	struct ua *ua;
	uint16_t close_scode;
	bool failed;

	unsigned n_incoming;
	unsigned n_progress;
	unsigned n_established;
	unsigned n_closed;
	unsigned n_dtmf_recv;
	unsigned n_transfer;
	unsigned n_mediaenc;
	unsigned n_rtpestab;
	unsigned n_rtcp;
};

struct fixture {
	uint32_t magic;
	struct agent a, b, c;
	struct sa laddr_udp;
	struct sa laddr_tcp;
	enum behaviour behaviour;
	enum action estab_action;
	char buri[256];
	char buri_tcp[256];
	int err;
	unsigned exp_estab;
	unsigned exp_closed;
	bool stop_on_rtp;
	bool stop_on_rtcp;
};


#define fixture_init_prm(f, prm)					\
	memset(f, 0, sizeof(*f));					\
									\
	f->a.fix = f;							\
	f->b.fix = f;							\
	f->c.fix = f;							\
									\
	err = ua_init("test", true, true, false);			\
	TEST_ERR(err);							\
									\
	f->magic = MAGIC;						\
	f->estab_action = ACTION_RECANCEL;				\
	f->exp_estab = 1;						\
	f->exp_closed = 1;						\
	/* NOTE: See Makefile TEST_MODULES */				\
	err = module_load(".", "g711");					\
	TEST_ERR(err);							\
									\
	err = ua_alloc(&f->a.ua,					\
		       "A <sip:a@127.0.0.1>;regint=0" prm);		\
	TEST_ERR(err);							\
	err = ua_alloc(&f->b.ua,					\
		       "B <sip:b@127.0.0.1>;regint=0" prm);		\
	TEST_ERR(err);							\
									\
	f->a.peer = &f->b;						\
	f->b.peer = &f->a;						\
									\
	err = uag_event_register(event_handler, f);			\
	TEST_ERR(err);							\
									\
	err = sip_transp_laddr(uag_sip(), &f->laddr_udp,		\
			       SIP_TRANSP_UDP, NULL);			\
	TEST_ERR(err);							\
									\
	err = sip_transp_laddr(uag_sip(), &f->laddr_tcp,		\
			       SIP_TRANSP_TCP, NULL);			\
	TEST_ERR(err);							\
									\
	debug("test: local SIP transp: UDP=%J, TCP=%J\n",		\
	      &f->laddr_udp, &f->laddr_tcp);				\
									\
	re_snprintf(f->buri, sizeof(f->buri),				\
		    "sip:b@%J", &f->laddr_udp);				\
	re_snprintf(f->buri_tcp, sizeof(f->buri_tcp),			\
		    "sip:b@%J;transport=tcp", &f->laddr_tcp);


#define fixture_init(f)				\
	fixture_init_prm((f), "");


#define fixture_close(f)			\
	mem_deref(f->c.ua);			\
	mem_deref(f->b.ua);			\
	mem_deref(f->a.ua);			\
						\
	module_unload("g711");			\
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

static const struct list *hdrs;


static const char dtmf_digits[] = "123";


static void event_handler(struct ua *ua, enum ua_event ev,
			  struct call *call, const char *prm, void *arg)
{
	struct fixture *f = arg;
	struct call *call2 = NULL;
	struct agent *ag;
	char curi[256];
	int err = 0;
	(void)prm;

#if 1
	info("test: [ %s ] event: %s (%s)\n",
		  ua_aor(ua), uag_event_str(ev), prm);
#endif

	ASSERT_TRUE(f != NULL);
	ASSERT_EQ(MAGIC, f->magic);

	if (ua == f->a.ua)
		ag = &f->a;
	else if (ua == f->b.ua)
		ag = &f->b;
	else if (ua == f->c.ua)
		ag = &f->c;
	else {
		return;
	}

	switch (ev) {

	case UA_EVENT_CALL_INCOMING:
		++ag->n_incoming;

		switch (f->behaviour) {

		case BEHAVIOUR_ANSWER:
			err = ua_answer(ua, call, VIDMODE_ON);
			if (err) {
				warning("ua_answer failed (%m)\n", err);
				goto out;
			}
			break;

		case BEHAVIOUR_PROGRESS:
			err = call_progress(call);
			if (err) {
				warning("call_progress failed (%m)\n", err);
				goto out;
			}
			break;

		case BEHAVIOUR_REJECT:
			ua_hangup(ua, call, 0, 0);
			call = NULL;
			ag->failed = true;
			break;

		case BEHAVIOUR_GET_HDRS:
			hdrs = call_get_custom_hdrs(call);
			err = ua_answer(ua, call, VIDMODE_ON);
			if (err) {
				warning("ua_answer failed (%m)\n", err);
				goto out;
			}
			break;

		default:
			break;
		}
		break;

	case UA_EVENT_CALL_PROGRESS:
		++ag->n_progress;

		re_cancel();
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		++ag->n_established;

		ASSERT_TRUE(str_isset(call_id(call)));

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

			case ACTION_TRANSFER:
				f->estab_action = ACTION_NOTHING;

				re_snprintf(curi, sizeof(curi),
					    "sip:c@%J", &f->laddr_udp);

				err = call_transfer(ua_call(f->a.ua), curi);
				if (err)
					goto out;
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

	case UA_EVENT_CALL_TRANSFER:
		++ag->n_transfer;

		err = ua_call_alloc(&call2, ua, VIDMODE_ON, NULL, call,
				    call_localuri(call), true);
		if (!err) {
			struct pl pl;

			pl_set_str(&pl, prm);

			err = call_connect(call2, &pl);
			if (err) {
				warning("ua: transfer: connect error: %m\n",
					err);
			}
		}

		if (err) {
			(void)call_notify_sipfrag(call, 500, "Call Error");
			mem_deref(call2);
		}
		break;

	case UA_EVENT_CALL_MENC:
		++ag->n_mediaenc;
		ASSERT_TRUE(stream_is_secure(audio_strm(call_audio(call))));
		break;

	case UA_EVENT_CALL_DTMF_START:
		ASSERT_EQ(1, str_len(prm));
		ASSERT_EQ(dtmf_digits[ag->n_dtmf_recv], prm[0]);
		++ag->n_dtmf_recv;

		if (ag->n_dtmf_recv >= str_len(dtmf_digits)) {
			re_cancel();
		}
		break;

	case UA_EVENT_CALL_RTPESTAB:
		++ag->n_rtpestab;

		if (f->stop_on_rtp && ag->peer->n_rtpestab > 0)
			re_cancel();
		break;

	case UA_EVENT_CALL_RTCP:
		++ag->n_rtcp;

		if (f->stop_on_rtcp && ag->peer->n_rtcp > 0)
			re_cancel();
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
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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
	char uri[256];
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_HANGUP_B;

	/* add angle brackets */
	re_snprintf(uri, sizeof(uri), "<%s>", f->buri);

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, uri, VIDMODE_OFF);
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
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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
		err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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
		err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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
		err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
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

	/* Set the max-calls limit */
	conf_config()->call.max_calls = 0;

	return err;
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
	err = mock_ausrc_register(&ausrc, baresip_ausrcl());
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

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


static void mock_vidisp_handler(const struct vidframe *frame,
				uint64_t timestamp, void *arg)
{
	struct fixture *fix = arg;
	int err = 0;
	(void)frame;
	(void)timestamp;
	(void)fix;

	ASSERT_EQ(MAGIC, fix->magic);

	ASSERT_EQ(conf_config()->video.enc_fmt, (int)frame->fmt);

	/* Stop the test */
	re_cancel();

 out:
	if (err)
		fixture_abort(fix, err);
}


int test_call_video(void)
{
	struct fixture fix, *f = &fix;
	struct vidsrc *vidsrc = NULL;
	struct vidisp *vidisp = NULL;
	int err = 0;

	conf_config()->video.fps = 100;
	conf_config()->video.enc_fmt = VID_FMT_YUV420P;

	fixture_init(f);

	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();
	err = mock_vidsrc_register(&vidsrc);
	TEST_ERR(err);
	err = mock_vidisp_register(&vidisp, mock_vidisp_handler, f);
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that video was enabled for this call */
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.b.n_established);

	ASSERT_TRUE(call_has_video(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_video(ua_call(f->b.ua)));

 out:
	fixture_close(f);
	mem_deref(vidisp);
	mem_deref(vidsrc);
	mock_vidcodec_unregister();

	return err;
}


static void mock_sample_handler(const void *sampv, size_t sampc, void *arg)
{
	struct fixture *fix = arg;
	bool got_aulevel;
	(void)sampv;
	(void)sampc;

	got_aulevel =
		0 == audio_level_get(call_audio(ua_call(fix->a.ua)), NULL) &&
		0 == audio_level_get(call_audio(ua_call(fix->b.ua)), NULL);

	if (got_aulevel)
		re_cancel();
}


int test_call_aulevel(void)
{
	struct fixture fix, *f = &fix;
	struct ausrc *ausrc = NULL;
	struct auplay *auplay = NULL;
	double lvl;
	int err = 0;

	/* Use a low packet time, so the test completes quickly */
	fixture_init_prm(f, ";ptime=1");

	conf_config()->audio.level = true;

	err = mock_ausrc_register(&ausrc, baresip_ausrcl());
	TEST_ERR(err);
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   mock_sample_handler, f);
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify audio silence */
	err = audio_level_get(call_audio(ua_call(f->a.ua)), &lvl);
	TEST_ERR(err);
	ASSERT_EQ(-96, lvl);
	err = audio_level_get(call_audio(ua_call(f->b.ua)), &lvl);
	TEST_ERR(err);
	ASSERT_EQ(-96, lvl);

 out:
	conf_config()->audio.level = false;

	fixture_close(f);
	mem_deref(auplay);
	mem_deref(ausrc);

	return err;
}


int test_call_progress(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_PROGRESS;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_progress);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(0, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_progress);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(0, fix.b.n_closed);

 out:
	fixture_close(f);

	return err;
}


static void audio_sample_handler(const void *sampv, size_t sampc, void *arg)
{
	struct fixture *fix = arg;
	int err = 0;
	(void)sampv;
	(void)sampc;

	ASSERT_EQ(MAGIC, fix->magic);

	/* Wait until the call is established and the incoming
	 * audio samples are successfully decoded.
	 */
	if (sampc && fix->a.n_established && fix->b.n_established &&
	    audio_rxaubuf_started(call_audio(ua_call(fix->a.ua))) &&
	    audio_rxaubuf_started(call_audio(ua_call(fix->b.ua)))
	    ) {
		re_cancel();
	}

 out:
	if (err)
		fixture_abort(fix, err);
}


static int test_media_base(enum audio_mode txmode)
{
	struct fixture fix, *f = &fix;
	struct ausrc *ausrc = NULL;
	struct auplay *auplay = NULL;
	int err = 0;

	fixture_init_prm(f, ";ptime=1");

	conf_config()->audio.txmode = txmode;

	conf_config()->audio.src_fmt = AUFMT_FLOAT;
	conf_config()->audio.play_fmt = AUFMT_FLOAT;

	err = mock_ausrc_register(&ausrc, baresip_ausrcl());
	TEST_ERR(err);
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   audio_sample_handler, f);
	TEST_ERR(err);

	f->estab_action = ACTION_NOTHING;

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(15000);
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
	conf_config()->audio.src_fmt = AUFMT_S16LE;
	conf_config()->audio.play_fmt = AUFMT_S16LE;

	fixture_close(f);
	mem_deref(auplay);
	mem_deref(ausrc);

	if (fix.err)
		return fix.err;

	return err;
}


int test_call_format_float(void)
{
	int err;

	err = test_media_base(AUDIO_MODE_POLL);
	ASSERT_EQ(0, err);

	conf_config()->audio.txmode = AUDIO_MODE_POLL;

 out:
	return err;
}


int test_call_mediaenc(void)
{
	struct fixture fix, *f = &fix;
	struct ausrc *ausrc = NULL;
	struct auplay *auplay = NULL;
	int err = 0;

	mock_menc_register();

	/* Enable a dummy media encryption protocol */
	fixture_init_prm(f, ";mediaenc=xrtp;ptime=1");

	ASSERT_STREQ("xrtp", account_mediaenc(ua_account(f->a.ua)));

	err = mock_ausrc_register(&ausrc, baresip_ausrcl());
	TEST_ERR(err);
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   audio_sample_handler, f);
	TEST_ERR(err);

	f->estab_action = ACTION_NOTHING;

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(0, fix.a.n_closed);

	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(0, fix.b.n_closed);

	/* verify that the call was encrypted */
	ASSERT_EQ(1, fix.a.n_mediaenc);
	ASSERT_EQ(1, fix.b.n_mediaenc);

 out:
	fixture_close(f);
	mem_deref(auplay);
	mem_deref(ausrc);

	mock_menc_unregister();

	if (fix.err)
		return fix.err;

	return err;
}


int test_call_medianat(void)
{
	struct fixture fix, *f = &fix;
	struct ausrc *ausrc = NULL;
	struct auplay *auplay = NULL;
	int err;

	mock_mnat_register(baresip_mnatl());

	/* Enable a dummy media NAT-traversal protocol */
	fixture_init_prm(f, ";medianat=XNAT;ptime=1");

	ASSERT_STREQ("XNAT", account_medianat(ua_account(f->a.ua)));

	err = mock_ausrc_register(&ausrc, baresip_ausrcl());
	TEST_ERR(err);
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   audio_sample_handler, f);
	TEST_ERR(err);

	f->estab_action = ACTION_NOTHING;

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(0, fix.a.n_closed);

	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(0, fix.b.n_closed);

 out:
	fixture_close(f);
	mem_deref(auplay);
	mem_deref(ausrc);

	mock_mnat_unregister();

	if (fix.err)
		return fix.err;

	return err;
}


int test_call_custom_headers(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;
	int some_id = 7;
	struct list custom_hdrs;
	bool headers_matched = true;

	fixture_init(f);

	ua_add_xhdr_filter(f->b.ua, "X-CALL_ID");
	ua_add_xhdr_filter(f->b.ua, "X-HEADER_NAME");

	f->behaviour = BEHAVIOUR_GET_HDRS;

	/* Make a call from A to B
	 * with some custom headers in INVITE message */

	list_init(&custom_hdrs);
	err  = custom_hdrs_add(&custom_hdrs, "X-CALL_ID", "%d", some_id);
	err |= custom_hdrs_add(&custom_hdrs, "X-HEADER_NAME", "%s", "VALUE");
	TEST_ERR(err);

	err = ua_set_custom_hdrs(f->a.ua, &custom_hdrs);
	TEST_ERR(err);

	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	list_flush(&custom_hdrs);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);

	if (!list_isempty(hdrs)) {
		struct le *le;
		for (le = list_head(hdrs); le; le = le->next) {
		    struct sip_hdr *hdr = le->data;
		    if (pl_strcasecmp(&hdr->name, "X-CALL_ID") == 0) {
		        char buf[20];
		        re_snprintf(buf, sizeof(buf), "%d", some_id);
		        if (pl_strcasecmp(&hdr->val, buf) != 0) {
		            headers_matched = false;
		        }
		    }
		    if (pl_strcasecmp(&hdr->name, "X-HEADER_NAME") == 0) {
		        if (pl_strcasecmp(&hdr->val, "VALUE") != 0) {
		            headers_matched = false;
		        }
		    }
		}
	}
	else {
		headers_matched = false;
	}

	ASSERT_TRUE(headers_matched);

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


int test_call_tcp(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call using TCP-transport */
	err = ua_connect(f->a.ua, 0, NULL, f->buri_tcp, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.b.n_established);

 out:
	fixture_close(f);

	return err;
}


/*
 *  Step 1. Call from A to B
 *
 *  Step 2. A transfer B to C
 *
 *  Step 3. Call between B and C
 *          No call for A
 */
int test_call_transfer(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	/* Create a 3rd useragent needed for transfer */
	err = ua_alloc(&f->c.ua, "C <sip:c@127.0.0.1>;regint=0");
	TEST_ERR(err);

	f->c.peer = &f->b;

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_TRANSFER;

	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.n_transfer);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(2, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(1, fix.b.n_transfer);

	ASSERT_EQ(1, fix.c.n_incoming);
	ASSERT_EQ(1, fix.c.n_established);
	ASSERT_EQ(0, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.n_transfer);

	ASSERT_EQ(0, list_count(ua_calls(f->a.ua)));
	ASSERT_EQ(1, list_count(ua_calls(f->b.ua)));
	ASSERT_EQ(1, list_count(ua_calls(f->c.ua)));

 out:
	fixture_close(f);

	return err;
}


int test_call_rtcp(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	/* Use a low packet time, so the test completes quickly */
	fixture_init_prm(f, ";ptime=1");

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;
	f->stop_on_rtcp = true;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that one or more RTCP packets were received */
	ASSERT_TRUE(fix.a.n_rtcp > 0);
	ASSERT_TRUE(fix.b.n_rtcp > 0);

 out:
	fixture_close(f);

	return err;
}


int test_call_aufilt(void)
{
	int err;

	mock_aufilt_register(baresip_aufiltl());

	err = test_media_base(AUDIO_MODE_POLL);
	ASSERT_EQ(0, err);

 out:
	mock_aufilt_unregister();

	return err;
}


/*
 * Simulate a complete WebRTC testcase
 */
int test_call_webrtc(void)
{
	struct fixture fix, *f = &fix;
	struct ausrc *ausrc = NULL;
	struct vidsrc *vidsrc = NULL;
	struct sdp_media *sdp_a, *sdp_b;
	int err;

	mock_mnat_register(baresip_mnatl());
	mock_menc_register();

	err = mock_ausrc_register(&ausrc, baresip_ausrcl());
	TEST_ERR(err);

	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();
	err = mock_vidsrc_register(&vidsrc);
	TEST_ERR(err);

	fixture_init_prm(f, ";medianat=XNAT;mediaenc=xrtp");

	f->estab_action = ACTION_NOTHING;
	f->behaviour = BEHAVIOUR_ANSWER;
	f->stop_on_rtp = true;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify MNAT */

	/* verify that MENC is secure */

	ASSERT_TRUE(
	  stream_is_secure(audio_strm(call_audio(ua_call(f->a.ua)))));
	ASSERT_TRUE(
	  stream_is_secure(audio_strm(call_audio(ua_call(f->b.ua)))));

	ASSERT_TRUE(
	  stream_is_secure(video_strm(call_video(ua_call(f->a.ua)))));
	ASSERT_TRUE(
	  stream_is_secure(video_strm(call_video(ua_call(f->b.ua)))));

	/* verify that one or more RTP packets were received */
	ASSERT_TRUE(fix.a.n_rtpestab > 0);
	ASSERT_TRUE(fix.b.n_rtpestab > 0);

	ASSERT_TRUE(call_has_video(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_video(ua_call(f->b.ua)));

	/* Verify SDP attributes */

	sdp_a = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	sdp_b = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));

	ASSERT_TRUE(NULL != sdp_media_rattr(sdp_a, "ssrc"));
	ASSERT_EQ(20, atoi(sdp_media_rattr(sdp_a, "ptime")));

	ASSERT_TRUE(NULL != sdp_media_rattr(sdp_b, "ssrc"));
	ASSERT_EQ(20, atoi(sdp_media_rattr(sdp_b, "ptime")));

 out:
	fixture_close(f);

	mem_deref(vidsrc);
	mem_deref(ausrc);
	mock_vidcodec_unregister();
	mock_menc_unregister();
	mock_mnat_unregister();

	if (fix.err)
		return fix.err;

	return err;
}
