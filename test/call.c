/**
 * @file test/call.c  Baresip selftest -- call
 *
 * Copyright (C) 2010 - 2015 Alfred E. Heggestad
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "test.h"
#include "call.h"
#include "sip/sipsrv.h"
#include "../src/core.h"  /* NOTE: temp */


#define DEBUG_MODULE "testcall"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


static int test_call_answer_priv(void)
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


int test_call_answer(void)
{
	int err;
	conf_config()->call.accept = true;
	err = test_call_answer_priv();
	if (err) {
		warning("call_accept true failed\n");
		return err;
	}

	conf_config()->call.accept = false;
	err = test_call_answer_priv();
	if (err) {
		warning("call_accept false failed\n");
		return err;
	}

	return 0;
}


static int test_call_reject_priv(bool headers)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	fixture_init(f);

	f->behaviour = headers ? BEHAVIOUR_REJECTF :  BEHAVIOUR_REJECT;

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

	ASSERT_EQ(headers ? 302 : 486, fix.a.close_scode);
	ASSERT_STREQ(headers ? "302 Moved Temporarily" :
			       "486 Busy Here", fix.a.close_prm);

 out:
	fixture_close(f);

	return err;
}


int test_call_reject(void)
{
	int err;
	err = test_call_reject_priv(false);
	if (err)
		return err;

	err = test_call_reject_priv(true);
	return err;
}


static int test_call_immediate_cancel(void)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	struct call *call;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_REJECT;

	cancel_rule_new(BEVENT_CALL_CLOSED, f->a.ua, 0, 0, 0);
	cr->n_closed = 1;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, &call, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	ua_hangup(f->a.ua, call, 0, NULL);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);

 out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);

	return err;
}


static int test_call_progress_cancel(void)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	struct call *call;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_PROGRESS;

	cancel_rule_new(BEVENT_CALL_PROGRESS, f->a.ua, 0, 0, 0);
	cr->n_progress = 1;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, &call, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ua_hangup(f->a.ua, call, 0, NULL);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_progress);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);

 out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);

	return err;
}


static int test_call_answer_cancel(void)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	struct call *call;
	int err = 0;

	fixture_init(f);

	f->behaviour = BEHAVIOUR_PROGRESS;

	cancel_rule_new(BEVENT_CALL_PROGRESS, f->a.ua, 0, 0, 0);
	cr->n_progress = 1;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, &call, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	err = ua_answer(f->b.ua, NULL, VIDMODE_ON);
	TEST_ERR(err);

	ua_hangup(f->a.ua, call, 0, NULL);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_progress);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);

 out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);

	return err;
}


int test_call_cancel(void)
{
	int err;

	err = test_call_immediate_cancel();
	TEST_ERR(err);

	err = test_call_progress_cancel();
	TEST_ERR(err);

	err = test_call_answer_cancel();
	TEST_ERR(err);

 out:
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
	enum { RTP_TIMEOUT_MS = 1 };
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


static void ausrc_square_handler(struct auframe *af, const char *dev,
				 void *arg)
{
	struct fixture *fix = arg;
	int err = 0;

	ASSERT_EQ(MAGIC, fix->magic);

	if (af->sampc == 0 || af->fmt != AUFMT_S16LE)
		return;

	struct pl plv = PL_INIT;
	re_regex(dev, str_len(dev), "vol=[0-9]+", &plv);

	struct pl plf = PL_INIT;
	re_regex(dev, str_len(dev), "freq=[0-9]+", &plf);

	int16_t *sampv = af->sampv;
	int16_t v = pl_isset(&plv) ? (int16_t) pl_u32(&plv) : 1000;
	uint32_t freq = pl_isset(&plf) ? pl_u32(&plf) : 1000;
	size_t di = af->srate * af->ch / (2 * freq);
	for (size_t i = 0; i < af->sampc; i++) {
		sampv[i] = v;

		if ((i+1) % di == 0)
			v = -v;
	}
 out:
	if (err)
		fixture_abort(fix, err);
}


static void mixdetect_handler(struct auframe *af, const char *dev, void *arg)
{
	struct fixture *fix = arg;
	struct agent *ag = NULL;
	int err = 0;

	err = fixture_auframe_handle(arg, af, dev, &ag);
	if (err)
		return;

	if (ag == &fix->a)
		return;

	struct ua *ua = ag->ua;
	int16_t *sampv = af->sampv;

	/* The mixed ausrc is a square wave with double frequency.
	 * Count how often the sample value changes. */
	uint32_t changes = 0;
	int16_t last_v = sampv[0];
	for (size_t i = 0; i < af->sampc; i++) {
		int16_t v = sampv[i];
		if (v != last_v) {
			++changes;
			last_v = v;
		}
	}

	bevent_ua_emit(BEVENT_CUSTOM, ua, changes > 2 ? "mixed" :
		       abs(last_v) > 900 ? "original" : "low",
		       ag->n_auframe);
}


int test_call_mixausrc(void)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	struct ausrc *ausrc = NULL;
	struct auplay *auplay = NULL;
	int err = 0;

	fixture_init_prm(f, ";ptime=2"
		       ";audio_source=mock-ausrc,freq=500"
		       ";audio_player=mock-auplay,a");
	mem_deref(f->b.ua);
	err = ua_alloc(&f->b.ua, "B <sip:b@127.0.0.1>;regint=0;ptime=2"
		       ";audio_source=mock-ausrc,freq=500"
		       ";audio_player=mock-auplay,b");
	TEST_ERR(err);

	conf_config()->avt.rtp_stats = true;

	cancel_rule_new(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "auframe";
	cr->n_auframe = 3;

	err = module_load(".", "mixausrc");
	TEST_ERR(err);
	err = mock_ausrc_register(&ausrc, baresip_ausrcl(),
				  ausrc_square_handler, f);
	TEST_ERR(err);

	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   mixdetect_handler, f);
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

	cancel_rule_pop();
	cancel_rule_new(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "mixed";

	fixture_delayed_command(f, 0,
				"mixausrc_enc_start mock-ausrc "
				"vol=500,freq=1000 50 100");
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	cancel_rule_pop();
	cancel_rule_new(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "original";
	fixture_delayed_command(f, 0, "mixausrc_enc_stop");

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

 out:
	fixture_close(f);
	mem_deref(ausrc);
	mem_deref(auplay);
	module_unload("mixausrc");
	return err;
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
	/* 4 incoming + 4 outgoing calls */
	conf_config()->call.max_calls = 8;

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
	/* set back to default */
	conf_config()->call.max_calls = 4;

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

	conf_config()->call.max_calls = 4;

	return err;
}


int test_call_dtmf(void)
{
	struct fixture fix, *f = &fix;
	int err = 0;

	/* Use a low packet time, so the test completes quickly */
	fixture_init_prm(f, ";ptime=1");
	f->dtmf_digits = "1234";

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
	size_t n = str_len(f->dtmf_digits);
	for (size_t i=0; i<n; i++) {
		err  = call_send_digit(ua_call(f->a.ua), f->dtmf_digits[i]);
		TEST_ERR(err);
	}

	err = call_send_digit(ua_call(f->a.ua), KEYCODE_REL);
	TEST_ERR(err);

	struct audio *audio = call_audio(ua_call(f->a.ua));
	ASSERT_TRUE(audio != NULL);
	ASSERT_TRUE(!audio_txtelev_empty(audio));

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_dtmf_recv);
	ASSERT_EQ((unsigned) n, fix.b.n_dtmf_recv);
	audio = call_audio(ua_call(f->a.ua));
	ASSERT_TRUE(audio != NULL);
	ASSERT_TRUE(audio_txtelev_empty(audio));

 out:
	fixture_close(f);
	module_unload("ausine");

	return err;
}


static void mock_vidisp_handler(const struct vidframe *frame,
				uint64_t timestamp, const char *title,
				void *arg)
{
	struct fixture *fix = arg;
	struct agent *ag;
	struct ua *ua;
	int err = 0;
	(void)frame;
	(void)timestamp;

	ASSERT_EQ(MAGIC, fix->magic);

	ASSERT_EQ(conf_config()->video.enc_fmt, (int)frame->fmt);

	if (title[4] == 'b')
		ag = &fix->b;
	else if (title[4] == 'c')
		ag = &fix->c;
	else
		ag = &fix->a;

	++ag->n_vidframe;
	ua = ag->ua;
	bevent_ua_emit(BEVENT_CUSTOM, ua, "vidframe %u", ag->n_vidframe);

 out:
	if (err)
		fixture_abort(fix, err);
}


int test_call_video(void)
{
	struct fixture fix, *f = &fix;
	struct vidisp *vidisp = NULL;
	struct cancel_rule *cr;
	int err = 0;

	conf_config()->video.fps = 100;
	conf_config()->video.enc_fmt = VID_FMT_YUV420P;

	fixture_init(f);
	cancel_rule_new(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "vidframe";
	cr->n_vidframe = 3;
	cancel_rule_and(BEVENT_CUSTOM, f->a.ua, 0, 0, 1);
	cr->prm = "vidframe";
	cr->n_vidframe = 3;

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
	struct sdp_media *vm;
	struct cancel_rule *cr, *cr_vida, *cr_vidb, *cr_prog;
	int err = 0;

	conf_config()->video.fps = 100;
	conf_config()->video.enc_fmt = VID_FMT_YUV420P;

	fixture_init_prm(f, ";answermode=early");
	cr_prog = cancel_rule_new(BEVENT_CALL_PROGRESS, f->a.ua, 0, 1, 0);

	cr_vidb = cancel_rule_new(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr_vidb->prm = "vidframe";
	cr_vidb->n_vidframe = 3;
	cr_vida = cancel_rule_and(BEVENT_CUSTOM, f->a.ua, 0, 1, 1);
	cr_vida->prm = "vidframe";
	cr_vida->n_vidframe = 3;

	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();

	err = mock_vidisp_register(&vidisp, mock_vidisp_handler, f);
	TEST_ERR(err);

	err = module_load(".", "fakevideo");
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_NOTHING;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* wait for CALL_PROGRESS */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);
	mem_deref(cr_prog);

	err = ua_answer(f->b.ua, ua_call(f->b.ua), VIDMODE_ON);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* wait for video frames */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that video was enabled and bi-directional */
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_TRUE(fix.a.n_vidframe >= 3);
	ASSERT_TRUE(fix.b.n_vidframe >= 3);

	ASSERT_TRUE(call_has_video(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_video(ua_call(f->b.ua)));

	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(vm));

	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(vm));

	cancel_rule_new(BEVENT_CALL_REMOTE_SDP, f->b.ua, 1, 0, 1);
	cr->prm = "offer";
	cancel_rule_and(BEVENT_CALL_REMOTE_SDP, f->a.ua, 0, 1, 1);
	cr->prm = "answer";

	/* Set video inactive */
	cr_vida->ev = BEVENT_MAX;
	cr_vidb->ev = BEVENT_MAX;
	err = call_set_video_dir(ua_call(f->a.ua), SDP_INACTIVE);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);
	err = agent_wait_for_ack(&f->a, -1, -1, 1);
	TEST_ERR(err);

	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_rdir(vm));

	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_rdir(vm));
	cancel_rule_pop();

	/* Set video sendrecv */
	f->a.n_vidframe = 0;
	f->b.n_vidframe = 0;
	cr_vida->ev = BEVENT_CUSTOM;
	cr_vidb->ev = BEVENT_CUSTOM;
	err = call_set_video_dir(ua_call(f->a.ua), SDP_SENDRECV);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);

	ASSERT_TRUE(call_has_video(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_video(ua_call(f->b.ua)));

	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(vm));

	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(vm));

 out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);
	mem_deref(vidisp);
	module_unload("fakevideo");
	mock_vidcodec_unregister();

	return err;
}


int test_call_100rel_video(void)
{
	struct fixture fix, *f = &fix;
	struct vidisp *vidisp = NULL;
	struct cancel_rule *cr;
	int err = 0;

	conf_config()->video.fps = 100;
	conf_config()->video.enc_fmt = VID_FMT_YUV420P;

	fixture_init_prm(f, ";100rel=yes;answermode=early");

	cancel_rule_new(BEVENT_CUSTOM, f->b.ua, 1, 0, 0);
	cr->prm = "vidframe";
	cr->n_vidframe = 3;
	cancel_rule_and(BEVENT_CUSTOM, f->a.ua, 0, 1, 0);
	cr->prm = "vidframe";
	cr->n_vidframe = 3;
	/* to enable video, we need one vidsrc and vidcodec */
	mock_vidcodec_register();

	err = mock_vidisp_register(&vidisp, mock_vidisp_handler, f);
	TEST_ERR(err);

	err = module_load(".", "fakevideo");
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_NOTHING;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* wait for video frames */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* switch off early video */
	cancel_rule_new(BEVENT_CALL_REMOTE_SDP, f->b.ua, 1, 0, 0);
	cr->prm = "offer";
	cancel_rule_and(BEVENT_CALL_REMOTE_SDP, f->a.ua, 0, 1, 0);
	cr->prm = "answer";

	err = call_set_video_dir(ua_call(f->a.ua), SDP_INACTIVE);
	TEST_ERR(err);
	/* wait for remote SDP at both UAs */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);
	err = agent_wait_for_ack(&f->a, -1, -1, 1);
	TEST_ERR(err);
	cancel_rule_pop();

	struct sdp_media *vm;
	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_rdir(vm));

	vm = stream_sdpmedia(video_strm(call_video(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(vm));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_rdir(vm));
	ASSERT_TRUE(call_refresh_allowed(ua_call(f->a.ua)));

	f->a.n_vidframe=0;
	f->b.n_vidframe=0;
	err = call_set_video_dir(ua_call(f->a.ua), SDP_SENDRECV);
	TEST_ERR(err);
	/* wait for video frames */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);
	ASSERT_TRUE(fix.a.n_vidframe >= 3);
	ASSERT_TRUE(fix.b.n_vidframe >= 3);
 out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);
	mem_deref(vidisp);
	module_unload("fakevideo");
	mock_vidcodec_unregister();

	return err;
}


static void auframe_handler(struct auframe *af, const char *dev, void *arg)
{
	struct fixture *fix = arg;

	fixture_auframe_handle(fix, af, dev, NULL);
}


int test_call_aulevel(void)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	struct auplay *auplay = NULL;
	int err = 0;

	/* Use a low packet time, so the test completes quickly */
	fixture_init_prm(f, ";ptime=1;audio_player=mock-auplay,a");
	mem_deref(f->b.ua);
	err = ua_alloc(&f->b.ua, "B <sip:b@127.0.0.1>"
		       ";regint=0;ptime=1;audio_player=mock-auplay,b");
	TEST_ERR(err);

	cancel_rule_new(BEVENT_CUSTOM, f->a.ua, 0, 0, 1);
	cr->prm = "auframe";
	cr->aulvl = -96.0f;
	cancel_rule_and(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "auframe";
	cr->aulvl = -96.0f;

	conf_config()->audio.level = true;

	err = module_load(".", "ausine");
	TEST_ERR(err);
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   auframe_handler, f);
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

 out:
	conf_config()->audio.level = false;

	fixture_close(f);
	mem_deref(auplay);
	module_unload("ausine");

	return err;
}


static int test_100rel_audio_base(enum audio_mode txmode)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	struct auplay *auplay = NULL;
	int err = 0;

	fixture_init_prm(f, ";ptime=1;audio_player=mock-auplay,a;100rel=yes");
	mem_deref(f->b.ua);
	err = ua_alloc(&f->b.ua,
		       "B <sip:b@127.0.0.1>"
		       ";regint=0;ptime=1;audio_player=mock-auplay,b"
		       ";answermode=early;100rel=yes");
	TEST_ERR(err);
	conf_config()->audio.txmode = txmode;

	cancel_rule_new(BEVENT_CUSTOM, f->b.ua, 1, -1, 0);
	cr->prm = "auframe";
	cr->n_auframe = 3;
	cancel_rule_and(BEVENT_CUSTOM, f->a.ua, 0, 1, 0);
	cr->prm = "auframe";
	cr->n_auframe = 3;

	err = module_load(".", "ausine");
	TEST_ERR(err);
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   auframe_handler, f);
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_NOTHING;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* wait for audio frames */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* switch off early audio */
	cancel_rule_new(BEVENT_CALL_REMOTE_SDP, f->b.ua, 1, -1, 0);
	cr->prm = "offer";
	cancel_rule_and(BEVENT_CALL_REMOTE_SDP, f->a.ua, 0, 1, 0);
	cr->prm = "answer";

	call_set_media_direction(ua_call(f->a.ua), SDP_INACTIVE, SDP_INACTIVE);
	TEST_ERR(err);
	err = call_modify(ua_call(f->a.ua));
	TEST_ERR(err);

	/* wait for remote SDP at both UAs */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);
	err = agent_wait_for_ack(&f->a, -1, -1, 1);
	TEST_ERR(err);
	cancel_rule_pop();

	struct sdp_media *am;
	am = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_ldir(am));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_rdir(am));

	am = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(am));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_rdir(am));
	ASSERT_TRUE(call_refresh_allowed(ua_call(f->a.ua)));

	f->a.n_auframe=0;
	f->b.n_auframe=0;
	call_set_media_direction(ua_call(f->a.ua), SDP_SENDRECV, SDP_INACTIVE);
	TEST_ERR(err);
	err = call_modify(ua_call(f->a.ua));
	TEST_ERR(err);

	/* wait for audio frames */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);
	ASSERT_TRUE(fix.a.n_auframe >= 3);
	ASSERT_TRUE(fix.b.n_auframe >= 3);
 out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);
	mem_deref(auplay);
	module_unload("ausine");

	return err;
}


int test_call_100rel_audio(void)
{
	int err;

	err = test_100rel_audio_base(AUDIO_MODE_POLL);
	ASSERT_EQ(0, err);

	err = test_100rel_audio_base(AUDIO_MODE_THREAD);
	ASSERT_EQ(0, err);

	conf_config()->audio.txmode = AUDIO_MODE_POLL;

 out:
	return err;
}


int test_call_progress(void)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	int err = 0;

	fixture_init_prm(f, ";answermode=early");
	cancel_rule_new(BEVENT_CALL_PROGRESS, f->a.ua, 0, 1, 0);

	f->behaviour = BEHAVIOUR_NOTHING;

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


static int test_media_base(enum audio_mode txmode,
			   enum aufmt sndfmt, enum aufmt acfmt)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	struct auplay *auplay = NULL;
	int err = 0;

	fixture_init_prm(f, ";ptime=5;audio_player=mock-auplay,a");
	mem_deref(f->b.ua);
	err = ua_alloc(&f->b.ua, "B <sip:b@127.0.0.1>"
		       ";regint=0;ptime=5;audio_player=mock-auplay,b");
	TEST_ERR(err);

	conf_config()->audio.srate_play = 16000;
	conf_config()->audio.srate_src = 16000;
	conf_config()->audio.txmode = txmode;
	conf_config()->audio.src_fmt = sndfmt;
	conf_config()->audio.channels_play = 1;
	conf_config()->audio.channels_src = 1;
	conf_config()->audio.play_fmt = sndfmt;
	conf_config()->audio.enc_fmt = acfmt;
	conf_config()->audio.dec_fmt = acfmt;
	conf_config()->avt.rtp_stats = true;

	cancel_rule_new(BEVENT_CUSTOM, f->a.ua, 0, 0, 1);
	cr->prm = "auframe";
	cr->n_auframe = 3;
	cancel_rule_and(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "auframe";
	cr->n_auframe = 3;

	err = module_load(".", "ausine");
	TEST_ERR(err);
	err = mock_auplay_register(&auplay, baresip_auplayl(),
				   auframe_handler, f);
	TEST_ERR(err);

	f->estab_action = ACTION_NOTHING;

	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(10000);
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
	if (err)
		failure_debug(f, false);

	conf_config()->audio.src_fmt = AUFMT_S16LE;
	conf_config()->audio.play_fmt = AUFMT_S16LE;
	conf_config()->audio.txmode = AUDIO_MODE_POLL;
	conf_config()->audio.srate_play = 0;
	conf_config()->audio.srate_src = 0;
	conf_config()->audio.channels_play = 0;
	conf_config()->audio.channels_src = 0;
	conf_config()->audio.enc_fmt = AUFMT_S16LE;
	conf_config()->audio.dec_fmt = AUFMT_S16LE;

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

	err = module_load(".", "auconv");
	TEST_ERR(err);

	err = module_load(".", "auresamp");
	TEST_ERR(err);

	mock_aucodec_register();

	err = test_media_base(AUDIO_MODE_POLL, AUFMT_S16LE, AUFMT_S16LE);
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_POLL, AUFMT_S16LE, AUFMT_FLOAT);
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_POLL, AUFMT_FLOAT, AUFMT_S16LE);
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_POLL, AUFMT_FLOAT, AUFMT_FLOAT);
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_THREAD, AUFMT_S16LE, AUFMT_S16LE);
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_THREAD, AUFMT_S16LE, AUFMT_FLOAT);
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_THREAD, AUFMT_FLOAT, AUFMT_S16LE);
	TEST_ERR(err);

	err = test_media_base(AUDIO_MODE_THREAD, AUFMT_FLOAT, AUFMT_FLOAT);
	TEST_ERR(err);

 out:
	mock_aucodec_unregister();
	module_unload("auresamp");
	module_unload("auconv");

	return err;
}


int test_call_mediaenc(void)
{
	struct fixture fix = {0}, *f = &fix;
	struct cancel_rule *cr;
	int err = 0;

	err = module_load(".", "srtp");
	TEST_ERR(err);

	/* Enable a dummy media encryption protocol */
	fixture_init_prm(f, ";mediaenc=srtp;ptime=1");
	cancel_rule_new(BEVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cancel_rule_and(BEVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);

	ASSERT_STREQ("srtp", account_mediaenc(ua_account(f->a.ua)));

	err = module_load(".", "ausine");
	TEST_ERR(err);
	err = module_load(".", "aufile");
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
	struct cancel_rule *cr;
	int err;

	mock_mnat_register(baresip_mnatl());

	/* Enable a dummy media NAT-traversal protocol */
	fixture_init_prm(f, ";medianat=XNAT;ptime=1");
	cancel_rule_new(BEVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cancel_rule_and(BEVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);

	ASSERT_STREQ("XNAT", account_medianat(ua_account(f->a.ua)));

	err = module_load(".", "ausine");
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

	if (!list_isempty(f->hdrs)) {
		struct le *le;
		for (le = list_head(f->hdrs); le; le = le->next) {
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
	/* 3 incoming + 3 outgoing calls */
	conf_config()->call.max_calls = 6;

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
	conf_config()->call.max_calls = 4;

	return err;
}


static void delayed_audio_debug(void *arg)
{
	struct agent *ag = arg;
	struct mbuf *mb;
	int err;

	if (!ua_call(ag->ua))
		return;

	mb = mbuf_alloc(1);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	err = mbuf_printf(mb, "%H", audio_debug, call_audio(ua_call(ag->ua)));
	mem_deref(mb);

	++ag->n_audebug;

	bevent_ua_emit(BEVENT_CUSTOM, ag->ua,  "audebug %u", ag->n_audebug);

	tmr_start(&ag->tmr, 2, delayed_audio_debug, ag);
out:
	if (err)
		ag->fix->err |= err;
}


static int test_call_rtcp_base(bool rtcp_mux)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	int err = 0;

	err = module_load(".", "ausine");
	TEST_ERR(err);

	/* Use a low packet time, so the test completes quickly */
	if (rtcp_mux) {
		fixture_init_prm(f, ";ptime=1;rtcp_mux=yes");
	}
	else {
		fixture_init_prm(f, ";ptime=1");
	}

	conf_config()->avt.rtp_stats = true;
	cancel_rule_new(BEVENT_CALL_ESTABLISHED, f->b.ua, 1, 0, 1);

	cancel_rule_new(BEVENT_CALL_RTCP, f->b.ua, 1, 0, 1);
	cr->n_rtcp = 5;
	cancel_rule_and(BEVENT_CALL_RTCP, f->a.ua, 0, 0, -1);
	cr->n_rtcp = 5;
	cancel_rule_and(BEVENT_CUSTOM,    f->b.ua, 1, 0, 1);
	cr->prm = "audebug";
	cr->n_audebug = 5;
	cancel_rule_and(BEVENT_CUSTOM,    f->a.ua, 0, 0, -1);
	cr->prm = "audebug";
	cr->n_audebug = 5;

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_OFF);
	TEST_ERR(err);

	stream_set_rtcp_interval(audio_strm(call_audio(ua_call(f->a.ua))), 2);

	/* wait for UA b ESTABLISHED */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	stream_set_rtcp_interval(audio_strm(call_audio(ua_call(f->b.ua))), 2);
	stream_start_rtcp(audio_strm(call_audio(ua_call(f->b.ua))));
	tmr_start(&f->a.tmr, 2, delayed_audio_debug, &f->a);
	tmr_start(&f->b.tmr, 2, delayed_audio_debug, &f->b);

	/* wait for RTCP on both sides */
	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that one or more RTCP packets were received */
	ASSERT_TRUE(fix.a.n_rtcp >= 5);
	ASSERT_TRUE(fix.b.n_rtcp >= 5);

 out:
	fixture_close(f);
	module_unload("ausine");

	return err;
}


int test_call_rtcp(void)
{
	int err = 0;

	err |= test_call_rtcp_base(false);
	err |= test_call_rtcp_base(true);

	return err;
}


/*
 * Simulate a complete WebRTC testcase
 */
int test_call_webrtc(void)
{
	struct fixture fix = {0}, *f = &fix;
	struct cancel_rule *cr;
	struct sdp_media *sdp_a, *sdp_b;
	int err;

	if (conf_config()->avt.rxmode == RECEIVE_MODE_THREAD)
		return 0;

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

	fixture_init_prm(f, ";medianat=XNAT;mediaenc=dtls_srtp;rtcp_mux=yes");
	cancel_rule_new(BEVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cr->n_audio_estab = cr->n_video_estab = 1;
	cancel_rule_and(BEVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);
	cr->n_audio_estab = cr->n_video_estab = 1;

	f->estab_action = ACTION_NOTHING;
	f->behaviour = BEHAVIOUR_ANSWER;

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
	struct cancel_rule *cr;
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

	cancel_rule_new(BEVENT_CALL_RTPESTAB, f->b.ua, 1, 0, -1);
	cancel_rule_and(BEVENT_CALL_RTPESTAB, f->a.ua, 0, 0, -1);
	cancel_rule_and(BEVENT_CALL_ESTABLISHED, f->b.ua, 1, 0, 1);
	cancel_rule_and(BEVENT_CALL_ESTABLISHED, f->a.ua, 0, 0, 1);

	f->estab_action = ACTION_NOTHING;
	f->behaviour = BEHAVIOUR_ANSWER;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	/* run main-loop with timeout, wait for events */
	err = re_main_timeout(15000);
	TEST_ERR(err);
	TEST_ERR(fix.err);
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(1, fix.b.n_established);

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
	mock_vidcodec_unregister();

	mock_mnat_unregister();
	module_unload("srtp");

	conf_config()->avt.bundle = false;
	conf_config()->avt.rtcp_mux = false;

	if (fix.err)
		return fix.err;

	if (err) {
		warning("test: call bundle test failed with mnat=%s menc=%s "
			"(%m)\n",
			use_mnat ? "on" : "off",
			use_menc ? "on" : "off", err);
	}

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

	if (conf_config()->avt.rxmode == RECEIVE_MODE_THREAD)
		return 0;

	err = test_call_bundle_base(false, false);
	TEST_ERR(err);

	err = test_call_bundle_base(true,  false);
	TEST_ERR(err);

	err = test_call_bundle_base(false, true);
	TEST_ERR(err);

	err = test_call_bundle_base(true,  true);
	TEST_ERR(err);

 out:
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
	struct cancel_rule *cr;
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
	found = net_laddr_apply(net, find_ipv6ll, &ipv6ll);
	ASSERT_TRUE(found);

	err = sip_transp_laddr(uag_sip(), &dst, SIP_TRANSP_UDP, &ipv6ll);
	TEST_ERR(err);

	/* Make a call from A to B */
	re_snprintf(uri, sizeof(uri), "sip:b@%J", &dst);
	err  = ua_alloc(&f->a.ua, "A <sip:a@kitchen>;regint=0");
	err |= ua_alloc(&f->b.ua, "B <sip:b@office>;regint=0");

	cancel_rule_new(BEVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cancel_rule_and(BEVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);

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


static int test_call_hold_resume_base(bool tcp)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr;
	int err = 0;


	fixture_init(f);
	cancel_rule_new(BEVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);
	cr->n_audio_estab = 1;
	cancel_rule_and(BEVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cr->n_audio_estab = 1;

	err = module_load(".", "ausine");
	TEST_ERR(err);
	err = module_load(".", "aufile");
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;

	/* Make a call from A to B */
	err = ua_connect(f->a.ua, 0, NULL, tcp ? f->buri_tcp : f->buri,
			 VIDMODE_ON);
	TEST_ERR(err);

	/* wait for RTP audio */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that audio was enabled and bi-directional */
	ASSERT_TRUE(call_has_audio(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_audio(ua_call(f->b.ua)));

	struct sdp_media *m;
	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));

	cancel_rule_new(BEVENT_CALL_REMOTE_SDP, f->b.ua, 1, 0, 1);
	cr->prm = "offer";
	cancel_rule_and(BEVENT_CALL_REMOTE_SDP, f->a.ua, 0, 0, 1);
	cr->prm = "answer";

	/* set call on-hold */
	err = call_hold(ua_call(f->a.ua), true);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	err = agent_wait_for_ack(&f->b, -1, -1, 1);
	TEST_ERR(err);

	ASSERT_EQ(0, f->a.n_hold_cnt);
	ASSERT_EQ(1, f->b.n_hold_cnt);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_RECVONLY, sdp_media_rdir(m));
	ASSERT_TRUE(!call_ack_pending(ua_call(f->b.ua)));

	/* set call to resume */
	err = call_hold(ua_call(f->a.ua), false);
	TEST_ERR(err);
	tmr_start(&f->b.tmr_ack, 1, check_ack, &f->b);
	err = re_main_timeout(10000);
	TEST_ERR(err);

	err = agent_wait_for_ack(&f->b, -1, -1, 1);
	TEST_ERR(err);

	ASSERT_EQ(0, f->a.n_resume_cnt);
	ASSERT_EQ(1, f->b.n_resume_cnt);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));
	ASSERT_TRUE(!call_ack_pending(ua_call(f->b.ua)));

	/* Hang up */
	cancel_rule_new(BEVENT_CALL_CLOSED, f->b.ua, 1, 0, 1);
	call_hangup(ua_call(f->a.ua), 0, NULL);
	tmr_start(&f->b.tmr_ack, 1, check_ack, &f->b);
	err = re_main_timeout(10000);
	TEST_ERR(err);


	/* New call from A -> B with sendonly offered */
	list_flush(&f->rules);
	cancel_rule_new(BEVENT_CALL_RTPESTAB, f->b.ua, 2, 0, 2);
	cr->n_audio_estab = 2;

	/* Make a call from A to B  */
	err = ua_connect_dir(f->a.ua, 0, NULL, f->buri_tcp,
			 VIDMODE_ON, SDP_SENDONLY, SDP_SENDONLY);
	TEST_ERR(err);

	/* wait for RTP audio */
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that audio was enabled */
	ASSERT_TRUE(call_has_audio(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_audio(ua_call(f->b.ua)));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_RECVONLY, sdp_media_rdir(m));

	cancel_rule_new(BEVENT_CALL_REMOTE_SDP, f->b.ua, 2, 0, 2);
	cr->prm = "offer";
	cancel_rule_and(BEVENT_CALL_REMOTE_SDP, f->a.ua, 0, 0, 2);
	cr->prm = "answer";

	/* set call on-hold from A */
	err = call_hold(ua_call(f->a.ua), true);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	err = agent_wait_for_ack(&f->b, -1, -1, 2);
	TEST_ERR(err);

	/* A sets sendonly stream on hold - same media direction */
	ASSERT_EQ(0, f->a.n_hold_cnt);
	ASSERT_EQ(1, f->b.n_hold_cnt);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_RECVONLY, sdp_media_rdir(m));
	ASSERT_TRUE(!call_ack_pending(ua_call(f->b.ua)));

	/* set call to resume from A */
	err = call_hold(ua_call(f->a.ua), false);
	TEST_ERR(err);
	tmr_start(&f->b.tmr_ack, 1, check_ack, &f->b);
	err = re_main_timeout(10000);
	TEST_ERR(err);

	err = agent_wait_for_ack(&f->b, -1, -1, 2);
	TEST_ERR(err);

	/* A wants to resume sendonly stream - same media direction */
	ASSERT_EQ(0, f->a.n_resume_cnt);
	ASSERT_EQ(1, f->b.n_resume_cnt);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_RECVONLY, sdp_media_rdir(m));
	ASSERT_TRUE(!call_ack_pending(ua_call(f->b.ua)));

	/* New cancel rules for hold from B */
	list_flush(&f->rules);
	cancel_rule_new(BEVENT_CALL_REMOTE_SDP, f->a.ua, 0, 0, 2);
	cr->prm = "offer";
	cancel_rule_and(BEVENT_CALL_REMOTE_SDP, f->b.ua, 2, 0, 2);
	cr->prm = "answer";

	/* set call on-hold from B */
	err = call_hold(ua_call(f->b.ua), true);
	TEST_ERR(err);
	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	err = agent_wait_for_ack(&f->a, -1, -1, 2);
	TEST_ERR(err);

	ASSERT_EQ(1, f->a.n_hold_cnt);
	ASSERT_EQ(1, f->b.n_hold_cnt);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_ldir(m));
	ASSERT_EQ(SDP_INACTIVE, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_ldir(m));
	ASSERT_EQ(SDP_RECVONLY, sdp_media_rdir(m));
	ASSERT_TRUE(!call_ack_pending(ua_call(f->b.ua)));

	/* set media inactive from B */
	call_set_media_direction(ua_call(f->b.ua), SDP_INACTIVE, SDP_INACTIVE);
	err = call_modify(ua_call(f->b.ua));
	TEST_ERR(err);

	err = re_main_timeout(10000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	err = agent_wait_for_ack(&f->a, -1, -1, 2);
	TEST_ERR(err);

	ASSERT_EQ(1, f->a.n_hold_cnt);
	ASSERT_EQ(1, f->b.n_hold_cnt);

	/* set call to resume from B */
	call_set_media_direction(ua_call(f->b.ua), SDP_SENDRECV, SDP_SENDRECV);
	err = call_hold(ua_call(f->b.ua), false);
	TEST_ERR(err);
	tmr_start(&f->a.tmr_ack, 1, check_ack, &f->a);
	err = re_main_timeout(10000);
	TEST_ERR(err);

	err = agent_wait_for_ack(&f->a, -1, -1, 2);
	TEST_ERR(err);

	ASSERT_EQ(1, f->a.n_resume_cnt);
	ASSERT_EQ(1, f->b.n_resume_cnt);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_RECVONLY, sdp_media_rdir(m));

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDONLY, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));
	ASSERT_TRUE(!call_ack_pending(ua_call(f->a.ua)));

 out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);
	module_unload("aufile");
	module_unload("ausine");

	return err;
}


int test_call_hold_resume(void)
{
	int err;

	err = test_call_hold_resume_base(false);
	TEST_ERR(err);

	err = test_call_hold_resume_base(true);
	TEST_ERR(err);

out:
	return err;
}


static bool sdp_crypto_handler(const char *name, const char *value, void *arg)
{
	char **key = arg;
	struct pl key_info = PL_INIT, key_prms = PL_INIT;
	int err = 0;

	(void)name;

	if (!str_isset(value))
		return false;

	err = re_regex(value, str_len(value), "[0-9]+ [^ ]+ [^ ]+[]*[^]*",
		NULL, NULL, &key_prms, NULL, NULL);
	if (err)
		return false;

	err = re_regex(key_prms.p, key_prms.l, "[^:]+:[^|]+[|]*[^|]*[|]*[^|]*",
		NULL, &key_info, NULL, NULL, NULL, NULL);
	if (err)
		return false;

	return 0 == pl_strdup(key, &key_info);
}


int test_call_srtp_tx_rekey(void)
{
	struct fixture fix, *f = &fix;
	struct cancel_rule *cr = NULL;
	struct auplay *auplay = NULL;

	char *a_rx_key = NULL, *a_tx_key = NULL;
	char *b_rx_key = NULL, *b_tx_key = NULL;
	char *a_rx_key_new = NULL, *a_tx_key_new = NULL;
	char *b_rx_key_new = NULL, *b_tx_key_new = NULL;
	int err = 0;

	err =  module_load(".", "srtp");
	err |= module_load(".", "ausine");
	TEST_ERR(err);

	err = mock_auplay_register(&auplay, baresip_auplayl(),
		auframe_handler, f);
	TEST_ERR(err);

	fixture_init_prm(f, ";mediaenc=srtp-mand"
		";ptime=1;audio_player=mock-auplay,a");
	f->b.ua = mem_deref(f->b.ua);
	err = ua_alloc(&f->b.ua, "B <sip:b@127.0.0.1>;mediaenc=srtp-mand"
		";regint=0;ptime=1;audio_player=mock-auplay,b");
	TEST_ERR(err);

	f->behaviour = BEHAVIOUR_ANSWER;
	f->estab_action = ACTION_NOTHING;

	/* call established cancel rule */
	cancel_rule_new(BEVENT_CALL_ESTABLISHED, f->a.ua, 0, 0, 1);
	cancel_rule_and(BEVENT_CALL_ESTABLISHED, f->b.ua, 1, 0, 1);

	/* Call A to B */
	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify audio was enabled and bi-directional */
	ASSERT_TRUE(call_has_audio(ua_call(f->a.ua)));
	ASSERT_TRUE(call_has_audio(ua_call(f->b.ua)));

	struct sdp_media *m;
	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));
	sdp_media_lattr_apply(m, "crypto", sdp_crypto_handler, &a_tx_key);
	sdp_media_rattr_apply(m, "crypto", sdp_crypto_handler, &a_rx_key);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));
	sdp_media_lattr_apply(m, "crypto", sdp_crypto_handler, &b_tx_key);
	sdp_media_rattr_apply(m, "crypto", sdp_crypto_handler, &b_rx_key);

	/* crosscheck rx & tx keys */
	TEST_STRCMP(a_rx_key, str_len(a_rx_key), b_tx_key, str_len(b_tx_key));
	TEST_STRCMP(a_tx_key, str_len(a_tx_key), b_rx_key, str_len(b_rx_key));

	/* rekeying transmission keys from a -> b */
	struct le *le = NULL;
	for (le = call_streaml(ua_call(f->a.ua))->head; le; le = le->next)
		stream_remove_menc_media_state(le->data);

	err = call_update_media(ua_call(f->a.ua));
	err |= call_modify(ua_call(f->a.ua));
	TEST_ERR(err);

	cancel_rule_new(BEVENT_CUSTOM, f->a.ua, 0, 0, 1);
	cr->prm = "auframe";
	cr->n_auframe = 10;
	cancel_rule_and(BEVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "auframe";
	cr->n_auframe = 10;

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->a.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));
	sdp_media_lattr_apply(m, "crypto", sdp_crypto_handler, &a_tx_key_new);
	sdp_media_rattr_apply(m, "crypto", sdp_crypto_handler, &a_rx_key_new);

	m = stream_sdpmedia(audio_strm(call_audio(ua_call(f->b.ua))));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_ldir(m));
	ASSERT_EQ(SDP_SENDRECV, sdp_media_rdir(m));
	sdp_media_lattr_apply(m, "crypto", sdp_crypto_handler, &b_tx_key_new);
	sdp_media_rattr_apply(m, "crypto", sdp_crypto_handler, &b_rx_key_new);

	/* transmission key of a must change */
	ASSERT_TRUE(0 != str_casecmp(a_tx_key, a_tx_key_new));

	/* transmission key of b must stay the same */
	TEST_STRCMP(b_tx_key, str_len(b_tx_key),
		    b_tx_key_new, str_len(b_tx_key_new));

	/* receiving key of b must be the new tx key of a*/
	TEST_STRCMP(b_rx_key_new, str_len(b_rx_key_new),
		    a_tx_key_new, str_len(a_tx_key_new));

	/* transmission key of a must be the new rx key of b*/
	TEST_STRCMP(a_tx_key_new, str_len(a_tx_key_new),
		    b_rx_key_new, str_len(b_rx_key_new));

out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);
	mem_deref(auplay);

	module_unload("ausine");
	module_unload("srtp");

	a_rx_key = mem_deref(a_rx_key);
	a_tx_key = mem_deref(a_tx_key);
	b_rx_key = mem_deref(b_rx_key);
	b_tx_key = mem_deref(b_tx_key);

	a_rx_key_new = mem_deref(a_rx_key_new);
	a_tx_key_new = mem_deref(a_tx_key_new);
	b_rx_key_new = mem_deref(b_rx_key_new);
	b_tx_key_new = mem_deref(b_tx_key_new);


	return err;
}


#ifdef USE_TLS
int test_call_sni(void)
{
	int err = 0;
	struct fixture fix, *f = &fix;
	struct dns_server *dns_srv = NULL;
	struct dnsc *dnsc = NULL;
	char buri_tls[256], curi_tls[256];
	const char *dp = test_datapath();
	char s[256];

	/* warnings are expected for negative test cases, so silence them */
	dbg_init(DBG_ERR, DBG_ANSI);

	/* Set wrong global certificate. */
	re_snprintf(conf_config()->sip.cert, sizeof(conf_config()->sip.cert),
		    "%s/sni/other-cert.pem", dp);
	conf_config()->sip.verify_server = true;

	/* Setup Mocking DNS Server */
	err = dns_server_alloc(&dns_srv, false);
	err |= dns_server_add_a(dns_srv, "retest.server.org", IP_127_0_0_1);
	err |= dns_server_add_a(dns_srv, "retest.unknown.org", IP_127_0_0_1);
	err |= dnsc_alloc(&dnsc, NULL, &dns_srv->addr, 1);
	err |= net_set_dnsc(baresip_network(), dnsc);
	TEST_ERR(err);

	fixture_init(f);

	mem_deref(f->a.ua);
	mem_deref(f->b.ua);

	f->behaviour = BEHAVIOUR_ANSWER;

	re_snprintf(s, sizeof(s), "A <sip:a@retest.client.org;transport=tls>"
		    ";regint=0;cert=%s/sni/client-interm.pem", dp);
	err = ua_alloc(&f->a.ua, s);
	TEST_ERR(err);

	re_snprintf(s, sizeof(s), "B <sip:b@retest.server.org;transport=tls>"
		    ";regint=0;cert=%s/sni/server-interm.pem", dp);
	err = ua_alloc(&f->b.ua, s);
	TEST_ERR(err);

	re_snprintf(s, sizeof(s), "C <sip:c@retest.unknown.org;"
		    "transport=tls>;regint=0;cert=%s/sni/other-cert.pem", dp);
	err = ua_alloc(&f->c.ua, s);
	TEST_ERR(err);

	re_snprintf(buri_tls, sizeof(buri_tls), "sip:b@retest.server.org:%u",
		    sa_port(&f->laddr_tls));
	re_snprintf(curi_tls, sizeof(curi_tls), "sip:c@retest.unknown.org:%u",
		    sa_port(&f->laddr_tls));

	/* 1st test. No CA set. Call from A to B. TLS handshake must fail. */
	f->b.n_closed = 1;

	err = ua_connect(f->a.ua, 0, NULL, buri_tls, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(0, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(0, fix.b.close_scode);

	ASSERT_EQ(0, fix.c.n_incoming);
	ASSERT_EQ(0, fix.c.n_established);
	ASSERT_EQ(0, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.close_scode);

	/* 2nd test. CA set. Call from A to C. TLS handshake must fail because
	 * certificate of C is selected which is from an unknown CA. */
	re_snprintf(s, sizeof(s), "%s/sni/root-ca.pem", dp);
	err = tls_add_cafile_path(uag_tls(), s, NULL);
	TEST_ERR(err);

	err = ua_connect(f->a.ua, 0, NULL, curi_tls, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(2, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(0, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(0, fix.b.close_scode);

	ASSERT_EQ(0, fix.c.n_incoming);
	ASSERT_EQ(0, fix.c.n_established);
	ASSERT_EQ(0, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.close_scode);

	/* 3rd test. CA set. Call from A to B. TLS handshake must succeed.
	* SNI chooses correct UA certificate even though global certificate
	* is set. */
	f->estab_action = ACTION_HANGUP_A;

	err = ua_connect(f->a.ua, 0, NULL, buri_tls, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(3, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(0, fix.b.close_scode);

	ASSERT_EQ(0, fix.c.n_incoming);
	ASSERT_EQ(0, fix.c.n_established);
	ASSERT_EQ(0, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.close_scode);

out:
	if (err)
		failure_debug(f, false);

	mem_deref(dns_srv);

	fixture_close(f);

	dbg_init(DEBUG_LEVEL, DBG_ANSI);

	return err;
}


int test_call_cert_select(void)
{
	int err = 0;
	struct fixture fix, *f = &fix;
	char auri_tls[256], buri_tls[256];
	const char *dp = test_datapath();
	char s[256];

	/* warnings are expected for negative test cases, so silence them */
	dbg_init(DBG_ERR, DBG_ANSI);

	/* Set valid global certificate. */
	re_snprintf(conf_config()->sip.cert, sizeof(conf_config()->sip.cert),
		    "%s/sni/server-interm.pem", dp);
	conf_config()->sip.verify_server = false;
	conf_config()->sip.verify_client = true;

	TEST_ERR(err);

	fixture_init(f);

	mem_deref(f->a.ua);
	mem_deref(f->b.ua);

	f->behaviour = BEHAVIOUR_ANSWER;

	re_snprintf(s, sizeof(s), "A <sip:a@127.0.0.1;transport=tls>"
		    ";regint=0;cert=%s/sni/client-interm.pem", dp);
	err = ua_alloc(&f->a.ua, s);
	TEST_ERR(err);

	re_snprintf(s, sizeof(s), "B <sip:b@127.0.0.1;transport=tls>"
		    ";regint=0;cert=%s/sni/other-cert.pem", dp);
	err = ua_alloc(&f->b.ua, s);
	TEST_ERR(err);

	re_snprintf(auri_tls, sizeof(auri_tls), "sip:a@127.0.0.1:%u",
		    sa_port(&f->laddr_tls));
	re_snprintf(buri_tls, sizeof(buri_tls), "sip:b@127.0.0.1:%u",
		    sa_port(&f->laddr_tls));

	/* 1st test. No CA set. Call from A to B. TLS handshake must fail. */
	f->b.n_closed = 1;

	err = ua_connect(f->a.ua, 0, NULL, buri_tls, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(0, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(1, fix.b.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(0, fix.c.n_incoming);
	ASSERT_EQ(0, fix.c.n_established);
	ASSERT_EQ(0, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.close_scode);

	/* 2nd test. CA set. Call from B to A. TLS handshake must fail because
	 * B has invalid cert set. */
	re_snprintf(s, sizeof(s), "%s/sni/root-ca.pem", dp);
	err = tls_add_cafile_path(uag_tls(), s, NULL);
	TEST_ERR(err);

	err = ua_connect(f->b.ua, 0, NULL, auri_tls, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(0, fix.b.n_incoming);
	ASSERT_EQ(0, fix.b.n_established);
	ASSERT_EQ(2, fix.b.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(0, fix.c.n_incoming);
	ASSERT_EQ(0, fix.c.n_established);
	ASSERT_EQ(0, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.close_scode);

	/* 3rd test. CA set. Call from A to B. TLS handshake must succeed. */
	f->estab_action = ACTION_HANGUP_A;

	err = ua_connect(f->a.ua, 0, NULL, buri_tls, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(1, fix.a.n_established);
	ASSERT_EQ(2, fix.a.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(1, fix.b.n_established);
	ASSERT_EQ(2, fix.b.n_closed);
	ASSERT_EQ(0, fix.a.close_scode);

	ASSERT_EQ(0, fix.c.n_incoming);
	ASSERT_EQ(0, fix.c.n_established);
	ASSERT_EQ(0, fix.c.n_closed);
	ASSERT_EQ(0, fix.c.close_scode);

out:
	if (err)
		failure_debug(f, false);

	fixture_close(f);

	dbg_init(DEBUG_LEVEL, DBG_ANSI);

	return err;
}
#endif


static void sip_server_exit_handler(void *arg)
{
	(void)arg;
	re_cancel();
}


static bool ua_cuser_has_suffix(const struct ua *ua)
{
	const char *cuser = ua_cuser(ua);
	size_t len = str_len(cuser);
	if (len < 16)
		return false;

	const struct account *acc = ua_account(ua);
	const struct pl *user = &account_luri(acc)->user;
	if (!user || !user->l)
		return false;

	return cuser[len - 16] == '-';
}


int test_call_uag_find_msg(void)
{
	struct fixture fix, *f = &fix;
	struct sip_server *srv1 = NULL;
	struct sip_server *srv2 = NULL;
	struct sa sa1;
	struct sa sa2;
	char *aor=NULL;
	char *curi=NULL;
	struct cancel_rule *cr;
	int err = 0;

	fixture_init(f);

	err = sip_server_alloc(&srv1, sip_server_exit_handler, NULL);
	TEST_ERR(err);

	err = sip_server_alloc(&srv2, sip_server_exit_handler, NULL);
	TEST_ERR(err);

	err = sip_transp_laddr(srv1->sip, &sa1, SIP_TRANSP_UDP, NULL);
	TEST_ERR(err);

	err = sip_transp_laddr(srv2->sip, &sa2, SIP_TRANSP_UDP, NULL);
	TEST_ERR(err);

	f->a.ua = mem_deref(f->a.ua);
	f->b.ua = mem_deref(f->b.ua);
	f->c.ua = mem_deref(f->c.ua);

	err = re_sdprintf(&aor, "A <sip:alice@%J>;regint=60", &sa1);
	TEST_ERR(err);
	err = ua_alloc(&f->a.ua, aor);
	TEST_ERR(err);
	aor = mem_deref(aor);
	err = re_sdprintf(&aor, "B <sip:alice@%J>;regint=60", &sa2);
	TEST_ERR(err);
	err = ua_alloc(&f->b.ua, aor);
	TEST_ERR(err);
	aor = mem_deref(aor);
	err = re_sdprintf(&aor, "C <sip:bob@%J>;regint=60", &sa2);
	TEST_ERR(err);
	err = ua_alloc(&f->c.ua, aor);
	TEST_ERR(err);
	ASSERT_TRUE(!ua_cuser_has_suffix(f->a.ua));
	ASSERT_TRUE(ua_cuser_has_suffix(f->b.ua));

	err = ua_register(f->a.ua);
	TEST_ERR(err);
	err = ua_register(f->b.ua);
	TEST_ERR(err);
	err = ua_register(f->c.ua);
	TEST_ERR(err);

	cancel_rule_new(BEVENT_REGISTER_OK, f->a.ua, 0, 0, 0);
	cancel_rule_and(BEVENT_REGISTER_OK, f->b.ua, 0, 0, 0);
	cancel_rule_and(BEVENT_REGISTER_OK, f->c.ua, 0, 0, 0);
	err = re_main_timeout(5000);
	TEST_ERR(err);

	cancel_rule_pop();

	f->b.peer = &f->c;
	f->c.peer = &f->b;

	f->behaviour = BEHAVIOUR_ANSWER;
	cancel_rule_new(BEVENT_CALL_ESTABLISHED, f->c.ua, 0, 0, 1);
	cancel_rule_and(BEVENT_CALL_ESTABLISHED, f->b.ua, 1, 0, 1);

	err = re_sdprintf(&curi, "sip:alice@%J", &sa2);
	TEST_ERR(err);
	err = ua_connect(f->c.ua, NULL, NULL, curi, VIDMODE_OFF);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	cancel_rule_pop();
	TEST_ERR(err);
	TEST_ERR(fix.err);

	/* verify that the right UA was selected and got established call */
	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.a.n_established);
	ASSERT_EQ(1, fix.b.n_incoming);
	ASSERT_EQ(1, fix.b.n_established);

	/* 2nd test: peer-to-peer call to registered UAs should be rejected */
	f->a.ua = mem_deref(f->a.ua);
	aor = mem_deref(aor);
	err = re_sdprintf(&aor, "A <sip:alice@%J>;regint=60", &sa1);
	TEST_ERR(err);
	err = ua_alloc(&f->a.ua, aor);
	TEST_ERR(err);
	err = ua_register(f->a.ua);
	TEST_ERR(err);
	cancel_rule_new(BEVENT_REGISTER_OK, f->a.ua, 0, 0, 0);
	err = re_main_timeout(5000);
	cancel_rule_pop();
	TEST_ERR(err);

	curi = mem_deref(curi);
	ASSERT_TRUE(ua_cuser_has_suffix(f->a.ua));
	ASSERT_TRUE(ua_cuser_has_suffix(f->b.ua));
	/* alice --> rejected. alice-<suffix> would be correct */
	err = re_sdprintf(&curi, "sip:alice@%J", &f->laddr_udp);
	TEST_ERR(err);

	f->b.n_incoming = 0;
	f->c.n_established = 0;
	cancel_rule_new(BEVENT_CALL_CLOSED, f->c.ua, 0, 0, 0);
	err = ua_connect(f->c.ua, NULL, NULL, curi, VIDMODE_OFF);
	TEST_ERR(err);
	err = re_main_timeout(5000);
	cancel_rule_pop();
	TEST_ERR(err);
	TEST_ERR(fix.err);

	ASSERT_EQ(0, fix.a.n_incoming);
	ASSERT_EQ(0, fix.b.n_incoming);
	ASSERT_EQ(0, fix.c.n_incoming);

 out:
	mem_deref(aor);
	mem_deref(srv1);
	mem_deref(srv2);
	fixture_close(f);
	mem_deref(curi);

	return err;
}
