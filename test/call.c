/**
 * @file test/call.c  Baresip selftest -- call
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "test.h"
#include "../src/core.h"  /* NOTE: temp */


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
	ACTION_TRANSFER,
	ACTION_ATT_TRANSFER,
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
	unsigned n_transfer_fail;
	unsigned n_dtmf_recv;
	unsigned n_transfer;
	unsigned n_mediaenc;
	unsigned n_rtpestab;
	unsigned n_rtcp;
	unsigned n_audio_estab;
	unsigned n_video_estab;
};

struct fixture {
	uint32_t magic;
	struct agent a, b, c;
	struct sa dst;
	struct sa laddr_udp;
	struct sa laddr_tcp;
	enum behaviour behaviour;
	enum action estab_action;
	char buri[256];
	char buri_tcp[256];
	int err;
	struct call *xfer;
	unsigned exp_estab;
	unsigned exp_closed;
	bool fail_transfer;
	bool stop_on_rtp;
	bool stop_on_rtcp;
	bool stop_on_audio_video;
	bool accept_session_updates;
};


#define fixture_init_prm(f, prm)					\
	memset(f, 0, sizeof(*f));					\
									\
	f->a.fix = f;							\
	f->b.fix = f;							\
	f->c.fix = f;							\
	err = sa_set_str(&f->dst, "127.0.0.1", 5060);			\
	TEST_ERR(err);							\
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
			       SIP_TRANSP_UDP, &f->dst);		\
	TEST_ERR(err);							\
									\
	err = sip_transp_laddr(uag_sip(), &f->laddr_tcp,		\
			       SIP_TRANSP_TCP, &f->dst);		\
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


static bool agent_audio_video_estab(const struct agent *ag)
{
	return ag->n_audio_estab > 0 && ag->n_video_estab > 0;
}


static void event_handler(struct ua *ua, enum ua_event ev,
			  struct call *call, const char *prm, void *arg)
{
	struct fixture *f = arg;
	struct call *call2 = NULL;
	struct agent *ag;
	struct stream *strm = NULL;
	char curi[256];
	int err = 0;
	(void)prm;

#if 1
	info("test: [ %s ] event: %s (%s)\n",
	     account_aor(ua_account(ua)), uag_event_str(ev), prm);
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

				if (f->fail_transfer)
					f->behaviour = BEHAVIOUR_REJECT;

				re_snprintf(curi, sizeof(curi),
					    "sip:c@%J", &f->laddr_udp);

				err = call_hold(ua_call(f->a.ua), true);
				if (err)
					goto out;

				err = call_transfer(ua_call(f->a.ua), curi);
				if (err)
					goto out;
				break;

			case ACTION_ATT_TRANSFER:
				re_snprintf(curi, sizeof(curi), "sip:c@%J",
					&f->laddr_udp);

				if (f->xfer) {
					err = call_hold(ua_call(ua), true);
					if (err)
						goto out;

					err = call_replace_transfer(f->xfer,
						ua_call(ua));
					if (err)
						goto out;

					f->xfer = NULL;
					f->estab_action = ACTION_NOTHING;
					break;
				}

				err = call_hold(ua_call(ua), true);
				if (err)
					goto out;

				err = ua_connect(ua, NULL, NULL,
					curi, VIDMODE_ON);
				if (err)
					goto out;

				f->xfer = call;

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

			call_set_user_data(call2, call_user_data(call));
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

	case UA_EVENT_CALL_TRANSFER_FAILED:
		++ag->n_transfer_fail;

		call_hold(call, false);
		if (ua == f->a.ua)
			re_cancel();

		break;

	case UA_EVENT_CALL_REMOTE_SDP:
		if (f->accept_session_updates &&
		    call_state(call) == CALL_STATE_ESTABLISHED &&
		    !call_is_outgoing(call) &&
		    sdp_media_ldir(stream_sdpmedia(video_strm(call_video(
				call)))) == SDP_SENDRECV &&
		    sdp_media_rdir(stream_sdpmedia(video_strm(call_video(
				call)))) == SDP_INACTIVE) {
			re_cancel();
		}
		break;
	case UA_EVENT_CALL_MENC:
		++ag->n_mediaenc;

		if (strstr(prm, "audio"))
			strm = audio_strm(call_audio(call));
		else if (strstr(prm, "video"))
			strm = video_strm(call_video(call));

		if (strm) {
			ASSERT_TRUE(stream_is_secure(strm));
		}
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

		if (strstr(prm, "audio"))
			++ag->n_audio_estab;
		else if (strstr(prm, "video"))
			++ag->n_video_estab;

		if (f->stop_on_rtp && ag->peer->n_rtpestab > 0)
			re_cancel();

		if (f->stop_on_audio_video) {

			if (agent_audio_video_estab(ag) &&
			    agent_audio_video_estab(ag->peer)) {

				re_cancel();
			}
		}
		break;

	case UA_EVENT_CALL_RTCP:
		++ag->n_rtcp;

		if (f->accept_session_updates &&
		    call_state(call) == CALL_STATE_ESTABLISHED &&
		    !call_is_outgoing(call) &&
		    0 == str_casecmp(prm, "video")&&
		    sdp_media_ldir(stream_sdpmedia(video_strm(call_video(
				ua_call(f->a.ua))))) == SDP_SENDRECV &&
		    sdp_media_rdir(stream_sdpmedia(video_strm(call_video(
				ua_call(f->a.ua))))) == SDP_SENDRECV &&
		    sdp_media_ldir(stream_sdpmedia(video_strm(call_video(
				ua_call(f->b.ua))))) == SDP_SENDRECV &&
		    sdp_media_rdir(stream_sdpmedia(video_strm(call_video(
				ua_call(f->b.ua))))) == SDP_SENDRECV) {
			re_cancel();
		}
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
		fixture_abort(f, err);
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

	/* Set the max-calls limit to accept 1 incoming call. */
	/* We start 2 calls from a.ua to b.ua. */
	/* This are 2 outgoing calls and 1 incoming. */
	conf_config()->call.max_calls = 3;

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
	size_t i, n = str_len(dtmf_digits);
	int err = 0;

	/* Use a low packet time, so the test completes quickly */
	fixture_init_prm(f, ";ptime=1");

	/* audio-source is needed for dtmf/telev to work */
	err = module_load(".", "ausine");
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
	module_unload("ausine");

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
	if (!fix->accept_session_updates) {
		re_cancel();
	}

 out:
	if (err)
		fixture_abort(fix, err);
}


int test_call_video(void)
{
	struct fixture fix, *f = &fix;
	struct vidisp *vidisp = NULL;
	int err = 0;

	conf_config()->video.fps = 100;
	conf_config()->video.enc_fmt = VID_FMT_YUV420P;

	fixture_init(f);

	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();

	err = mock_vidisp_register(&vidisp, mock_vidisp_handler, f);
	TEST_ERR(err);

	err = module_load(".", "fakevideo");
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
	module_unload("fakevideo");
	mock_vidcodec_unregister();

	return err;
}


int test_call_change_videodir(void)
{
	struct fixture fix, *f = &fix;
	struct vidisp *vidisp = NULL;
	enum sdp_dir a_video_ldir, a_video_rdir, b_video_ldir, b_video_rdir;
	int err = 0;

	conf_config()->video.fps = 100;
	conf_config()->video.enc_fmt = VID_FMT_YUV420P;

	fixture_init(f);

	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();

	err = mock_vidisp_register(&vidisp, mock_vidisp_handler, f);
	TEST_ERR(err);

	err = module_load(".", "fakevideo");
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_PROGRESS;
	f->estab_action = ACTION_NOTHING;
	f->accept_session_updates = true;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for PROGRESS to re_cancel loop */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	err = ua_answer(f->b.ua, ua_call(f->b.ua), VIDMODE_ON);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* run main-loop with timeout, wait for answer to re_canel loop */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that video was enabled for this call */
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.b.n_established);

	ASSERT_TRUE(call_has_video(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_video(ua_call(f->b.ua)));

	/* Set video inactive */
	err = call_set_video_dir(ua_call(f->a.ua), SDP_INACTIVE);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);

	a_video_ldir = sdp_media_ldir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->a.ua)))));
	a_video_rdir = sdp_media_rdir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->a.ua)))));
	b_video_ldir = sdp_media_ldir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->b.ua)))));
	b_video_rdir = sdp_media_rdir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->b.ua)))));

	ASSERT_TRUE(call_has_video(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_video(ua_call(f->b.ua)));
	ASSERT_TRUE(a_video_ldir == SDP_INACTIVE);
	ASSERT_TRUE(a_video_rdir == SDP_SENDRECV);
	ASSERT_TRUE(b_video_ldir == SDP_SENDRECV);
	ASSERT_TRUE(b_video_rdir == SDP_INACTIVE);

	/* Set video sendrecv */
	err = call_set_video_dir(ua_call(f->a.ua), SDP_SENDRECV);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);

	a_video_ldir = sdp_media_ldir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->a.ua)))));
	a_video_rdir = sdp_media_rdir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->a.ua)))));
	b_video_ldir = sdp_media_ldir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->b.ua)))));
	b_video_rdir = sdp_media_rdir(stream_sdpmedia(
			video_strm(call_video(ua_call(f->b.ua)))));

	ASSERT_TRUE(call_has_video(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_video(ua_call(f->b.ua)));
	ASSERT_TRUE(a_video_ldir == SDP_SENDRECV);
	ASSERT_TRUE(a_video_rdir == SDP_SENDRECV);
	ASSERT_TRUE(b_video_ldir == SDP_SENDRECV);
	ASSERT_TRUE(b_video_rdir == SDP_SENDRECV);

 out:
	fixture_close(f);
	mem_deref(vidisp);
	module_unload("fakevideo");
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
	struct auplay *auplay = NULL;
	double lvl;
	int err = 0;

	/* Use a low packet time, so the test completes quickly */
	fixture_init_prm(f, ";ptime=1");

	conf_config()->audio.level = true;

	err = module_load(".", "ausine");
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
	ASSERT_TRUE(lvl > -96.0f && lvl < 0.0f);
	err = audio_level_get(call_audio(ua_call(f->b.ua)), &lvl);
	TEST_ERR(err);
	ASSERT_TRUE(lvl > -96.0f && lvl < 0.0f);

 out:
	conf_config()->audio.level = false;

	fixture_close(f);
	mem_deref(auplay);
	module_unload("ausine");

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
	struct auplay *auplay = NULL;
	int err = 0;

	fixture_init_prm(f, ";ptime=1");

	conf_config()->audio.txmode = txmode;

	conf_config()->audio.src_fmt = AUFMT_S16LE;
	conf_config()->audio.play_fmt = AUFMT_S16LE;

	err = module_load(".", "ausine");
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
	module_unload("ausine");

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
	struct fixture fix = {0}, *f = &fix;
	int err = 0;

	err = module_load(".", "srtp");
	TEST_ERR(err);

	/* Enable a dummy media encryption protocol */
	fixture_init_prm(f, ";mediaenc=srtp;ptime=1");

	ASSERT_STREQ("srtp", account_mediaenc(ua_account(f->a.ua)));

	err = module_load(".", "ausine");
	TEST_ERR(err);
	err = module_load(".", "aufile");
	TEST_ERR(err);

	f->estab_action = ACTION_NOTHING;

	f->behaviour = BEHAVIOUR_ANSWER;
	f->stop_on_rtp = true;

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

	ASSERT_TRUE(fix.a.n_rtpestab > 0);
	ASSERT_TRUE(fix.b.n_rtpestab > 0);

 out:
	fixture_close(f);
	module_unload("aufile");
	module_unload("ausine");

	module_unload("srtp");

	if (fix.err)
		return fix.err;

	return err;
}


int test_call_medianat(void)
{
	struct fixture fix, *f = &fix;
	int err;

	mock_mnat_register(baresip_mnatl());

	/* Enable a dummy media NAT-traversal protocol */
	fixture_init_prm(f, ";medianat=XNAT;ptime=1");

	ASSERT_STREQ("XNAT", account_medianat(ua_account(f->a.ua)));

	err = module_load(".", "ausine");
	TEST_ERR(err);

	f->estab_action = ACTION_NOTHING;

	f->behaviour = BEHAVIOUR_ANSWER;
	f->stop_on_rtp = true;

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
	module_unload("ausine");

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


int test_call_deny_udp(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;
	char curi[256];

	fixture_init(f);

	mem_deref(f->a.ua);
	mem_deref(f->b.ua);
	err = ua_alloc(&f->a.ua, "A <sip:a@127.0.0.1;transport=tcp>;regint=0");
	TEST_ERR(err);
	err = ua_alloc(&f->b.ua, "B <sip:b@127.0.0.1;transport=tcp>;regint=0");
	TEST_ERR(err);

	f->a.peer = &f->b;
	f->b.peer = &f->a;

	f->b.n_closed = 1;
	f->estab_action = ACTION_RECANCEL;

	/* Make a call using UDP-transport */
	re_snprintf(curi, sizeof(curi), "sip:b@%J;transport=udp",
			&f->laddr_udp);
	err = ua_connect(f->a.ua, 0, NULL, curi, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(0, fix.b.n_incoming);

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


int test_call_transfer_fail(void)
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
	f->fail_transfer = true;

	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(0, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.n_transfer);
	ASSERT_TRUE(!call_is_onhold(ua_call(f->a.ua)));
	ASSERT_EQ(CALL_STATE_ESTABLISHED, call_state(ua_call(f->a.ua)));

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(1, fix.b.n_transfer);
	ASSERT_EQ(1, fix.b.n_transfer_fail);
	ASSERT_EQ(CALL_STATE_ESTABLISHED, call_state(ua_call(f->b.ua)));

	ASSERT_EQ(1, fix.c.n_incoming);
	ASSERT_EQ(0, fix.c.n_established);
	ASSERT_EQ(1, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.n_transfer);

	ASSERT_EQ(1, list_count(ua_calls(f->a.ua)));
	ASSERT_EQ(1, list_count(ua_calls(f->b.ua)));
	ASSERT_EQ(0, list_count(ua_calls(f->c.ua)));

out:
	fixture_close(f);

	return err;
}


int test_call_attended_transfer(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	err = ua_alloc(&f->c.ua, "C <sip:c@127.0.0.1>;regint=0");
	TEST_ERR(err);

	f->c.peer = &f->a;

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_ATT_TRANSFER;
	f->fail_transfer = false;

	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(2, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(1, fix.a.n_transfer);
	ASSERT_EQ(CALL_STATE_ESTABLISHED, call_state(ua_call(f->a.ua)));

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(2, fix.b.n_established);
	ASSERT_EQ(2, fix.b.n_closed);
	ASSERT_EQ(0, fix.b.n_transfer);

	ASSERT_EQ(2, fix.c.n_incoming);
	ASSERT_EQ(2, fix.c.n_established);
	ASSERT_EQ(1, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.n_transfer);
	ASSERT_EQ(CALL_STATE_ESTABLISHED, call_state(ua_call(f->c.ua)));

	ASSERT_EQ(1, list_count(ua_calls(f->a.ua)));
	ASSERT_EQ(0, list_count(ua_calls(f->b.ua)));
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

	err = module_load(".", "auconv");
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_POLL);
	ASSERT_EQ(0, err);

 out:
	module_unload("auconv");

	return err;
}


/*
 * Simulate a complete WebRTC testcase
 */
int test_call_webrtc(void)
{
	struct fixture fix = {0}, *f = &fix;
	struct sdp_media *sdp_a, *sdp_b;
	int err;

	conf_config()->avt.rtcp_mux = true;

	mock_mnat_register(baresip_mnatl());

	err = module_load(".", "dtls_srtp");
	TEST_ERR(err);

	err = module_load(".", "ausine");
	TEST_ERR(err);

	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();
	err = module_load(".", "fakevideo");
	TEST_ERR(err);

	fixture_init_prm(f, ";medianat=XNAT;mediaenc=dtls_srtp");

	f->estab_action = ACTION_NOTHING;
	f->behaviour = BEHAVIOUR_ANSWER;
	f->stop_on_audio_video = true;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(15000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify MNAT */

	/* verify that MENC is secure */
#if 0
	ASSERT_TRUE(
	  stream_is_secure(audio_strm(call_audio(ua_call(f->a.ua)))));
	ASSERT_TRUE(
	  stream_is_secure(audio_strm(call_audio(ua_call(f->b.ua)))));

	ASSERT_TRUE(
	  stream_is_secure(video_strm(call_video(ua_call(f->a.ua)))));
	ASSERT_TRUE(
	  stream_is_secure(video_strm(call_video(ua_call(f->b.ua)))));
#endif

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

	module_unload("fakevideo");
	module_unload("ausine");
	mock_vidcodec_unregister();
	module_unload("dtls_srtp");
	mock_mnat_unregister();

	conf_config()->avt.rtcp_mux = false;

	if (fix.err)
		return fix.err;

	return err;
}


static int test_call_bundle_base(bool use_mnat, bool use_menc)
{
	struct fixture fix = {0}, *f = &fix;
	struct ausrc *ausrc = NULL;
	struct vidisp *vidisp = NULL;
	struct mbuf *sdp = NULL;
	struct call *callv[2];
	struct audio *audiov[2];
	struct video *videov[2];
	unsigned i;
	int err;

	conf_config()->avt.bundle = true;
	conf_config()->avt.rtcp_mux = true;  /* MUST enable RTP/RTCP mux */
	conf_config()->video.fps = 100;

	if (use_mnat) {
		mock_mnat_register(baresip_mnatl());
	}
	if (use_menc) {
		err = module_load(".", "srtp");
		TEST_ERR(err);
	}

	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();
	err = mock_vidisp_register(&vidisp, mock_vidisp_handler, f);
	TEST_ERR(err);

	err = module_load(".", "fakevideo");
	TEST_ERR(err);

	if (use_mnat && use_menc) {
		fixture_init_prm(f, ";medianat=XNAT;mediaenc=srtp");
	}
	else if (use_mnat) {
		fixture_init_prm(f, ";medianat=XNAT");
	}
	else if (use_menc) {
		fixture_init_prm(f, ";mediaenc=srtp");
	}
	else {
		fixture_init_prm(f, "");
	}

	f->estab_action = ACTION_NOTHING;
	f->behaviour = BEHAVIOUR_ANSWER;

	f->stop_on_rtp = true;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(15000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	callv[0] = ua_call(f->a.ua);
	callv[1] = ua_call(f->b.ua);

	/* Verify SDP attributes */
	for (i=0; i<2; i++) {

		audiov[i] = call_audio(callv[i]);
		videov[i] = call_video(callv[i]);

		ASSERT_TRUE(call_has_video(callv[i]));

		err = call_sdp_get(callv[i], &sdp, true);
		TEST_ERR(err);

		err = re_regex((char *)sdp->buf, sdp->end,
			       "a=group:BUNDLE 0 1");
		if (err) {
			warning("test: BUNDLE missing in SDP\n");
			re_printf("%b\n", sdp->buf, sdp->end);
			goto out;
		}

		err = re_regex((char *)sdp->buf, sdp->end,
			       "urn:ietf:params:rtp-hdrext:sdes:mid");
		TEST_ERR(err);

		sdp = mem_deref(sdp);
	}

	for (i=0; i<2; i++) {
		struct sdp_media *sdp_a, *sdp_v;

		sdp_a = stream_sdpmedia(audio_strm(audiov[i]));
		sdp_v = stream_sdpmedia(video_strm(videov[i]));

		ASSERT_STREQ("0", sdp_media_rattr(sdp_a, "mid"));
		ASSERT_STREQ("1", sdp_media_rattr(sdp_v, "mid"));
	}

	/* verify that remote addr au/vid is the same */
	for (i=0; i<2; i++) {
		const struct sa *saa, *sav;

		saa = stream_raddr(audio_strm(audiov[i]));
		sav = stream_raddr(video_strm(videov[i]));

		ASSERT_TRUE(sa_cmp(saa, sav, SA_ALL));

		ASSERT_TRUE(stream_is_ready(audio_strm(audiov[i])));
		ASSERT_TRUE(stream_is_ready(video_strm(videov[i])));
	}

	/* verify media */

	/* verify that one or more RTP packets were received */
	ASSERT_TRUE(fix.a.n_rtpestab > 0);
	ASSERT_TRUE(fix.b.n_rtpestab > 0);

	if (use_menc) {

		ASSERT_TRUE(stream_is_secure(audio_strm(audiov[0])));
		ASSERT_TRUE(stream_is_secure(audio_strm(audiov[1])));

		ASSERT_TRUE(stream_is_secure(video_strm(videov[0])));
		ASSERT_TRUE(stream_is_secure(video_strm(videov[1])));
	}

 out:
	fixture_close(f);

	mem_deref(sdp);
	mem_deref(vidisp);
	module_unload("fakevideo");
	mem_deref(ausrc);
	mock_vidcodec_unregister();

	mock_mnat_unregister();
	module_unload("srtp");

	conf_config()->avt.bundle = false;
	conf_config()->avt.rtcp_mux = false;

	if (fix.err)
		return fix.err;

	return err;
}


/*
 * Simple testcase for SDP Bundle
 *
 * audio: yes
 * video: yes
 * mnat:  optional
 * menc:  optional
 */
int test_call_bundle(void)
{
	int err = 0;

	err |= test_call_bundle_base(false, false);
	err |= test_call_bundle_base(true,  false);
	err |= test_call_bundle_base(false, true);
	err |= test_call_bundle_base(true,  true);

	return err;
}


static bool find_ipv6ll(const char *ifname, const struct sa *sa, void *arg)
{
	struct sa *ipv6ll = arg;
	(void) ifname;

	if (sa_af(sa) == AF_INET6 && sa_is_linklocal(sa)) {
		sa_cpy(ipv6ll, sa);
		return true;
	}

	return false;
}


int test_call_ipv6ll(void)
{
	struct fixture fix = {0}, *f = &fix;
	struct network *net = baresip_network();
	struct sa ipv6ll;
	bool found;
	struct sa dst;
	char uri[50];
	int err = 0;

	if (!net_laddr_af(net, AF_INET6)) {
		info("no IPv6 address -- skipping test %s\n", __func__);
		return 0;
	}

	err = module_load(".", "ausine");
	TEST_ERR(err);

	fixture_init(f);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;
	f->stop_on_rtp = true;
	found = net_laddr_apply(net, find_ipv6ll, &ipv6ll);
	ASSERT_TRUE(found);

	err = sip_transp_laddr(uag_sip(), &dst, SIP_TRANSP_UDP, &ipv6ll);
	TEST_ERR(err);

	/* Make a call from A to B */
	re_snprintf(uri, sizeof(uri), "sip:b@%J", &dst);
	err  = ua_alloc(&f->a.ua, "A <sip:a@kitchen>;regint=0");
	err |= ua_alloc(&f->b.ua, "B <sip:b@office>;regint=0");
	err |= ua_connect(f->a.ua, 0, NULL, uri, VIDMODE_OFF);
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
	ASSERT_EQ(0, fix.b.close_scode);

	ASSERT_TRUE(fix.a.n_rtpestab > 0);
	ASSERT_TRUE(fix.b.n_rtpestab > 0);
	sa_cpy(&ipv6ll,
	       stream_raddr(audio_strm(call_audio(ua_call(fix.a.ua)))));
	ASSERT_TRUE(sa_is_linklocal(&ipv6ll) && sa_af(&ipv6ll) == AF_INET6);
	sa_cpy(&ipv6ll,
	       stream_raddr(audio_strm(call_audio(ua_call(fix.b.ua)))));
	ASSERT_TRUE(sa_is_linklocal(&ipv6ll) && sa_af(&ipv6ll) == AF_INET6);

 out:
	fixture_close(f);
	module_unload("ausine");

	return err;
}
