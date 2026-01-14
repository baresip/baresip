/**
 * @file call_fixture.c  Call tests -- Test fixture and other helpers
 *
 * Copyright (C) 2026 Alfred E. Heggestad, Christian Spielberger
 */


#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"
#include "call.h"

#define DEBUG_MODULE "testcall"
#define DEBUG_LEVEL 5
#include <re_dbg.h>

static void delayed_dtmf_check(void *arg)
{
	struct agent *ag = arg;
	struct call *call = ua_call(ag->ua);

	if (audio_txtelev_empty(call_audio(call)))
		re_cancel();
	else
		tmr_start(&ag->tmr, 2, delayed_dtmf_check, ag);
}


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct fixture *f = arg;
	struct call *call2 = NULL;
	struct agent *ag;
	struct stream *strm = NULL;
	char curi[256];
	const char    *prm  = bevent_get_text(event);
	struct call   *call = bevent_get_call(event);
	struct ua     *ua   = bevent_get_ua(event);
	const struct sip_msg *msg  = bevent_get_msg(event);
	int err = 0;

#if 1
	info("test: [ %s ] event: %s (%s)\n",
	     account_aor(ua_account(ua)), bevent_str(ev), prm);
#endif

	ASSERT_TRUE(f != NULL);
	ASSERT_EQ(MAGIC, f->magic);

	if (ev == BEVENT_CREATE)
		return;

	if (!ua)
		ua = uag_find_msg(msg);

	if (ua && ev == BEVENT_SIPSESS_CONN) {
		err = ua_accept(ua, msg);
		if (err) {
			warning("test: could not accept incoming call (%m)\n",
				err);
			return;
		}

		bevent_stop(event);
	}

	if (ua == f->a.ua)
		ag = &f->a;
	else if (ua == f->b.ua)
		ag = &f->b;
	else if (ua == f->c.ua)
		ag = &f->c;
	else {
		warning("test: could not find agent/ua\n");
		return;
	}

	switch (ev) {

	case BEVENT_CALL_REDIRECT:
		ASSERT_STREQ("302,sip:c@127.0.0.1", prm);
		break;

	case BEVENT_CALL_INCOMING:
		++ag->n_incoming;

		switch (f->behaviour) {

		case BEHAVIOUR_ANSWER:
			err = ua_answer(ua, call, VIDMODE_ON);
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

		case BEHAVIOUR_REJECTF:
			ua_hangupf(ua, call, 302, "Moved Temporarily",
				"Contact: <sip:c@127.0.0.1>;expires=5\r\n"
				"Diversion: <sip:a@127.0.0.1>;reason=nop\r\n"
				"Content-Length: 0\r\n\r\n");
			call = NULL;
			ag->failed = true;
			break;

		case BEHAVIOUR_GET_HDRS:
			f->hdrs = call_get_custom_hdrs(call);
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

		default:
			break;
		}
		break;

	case BEVENT_CALL_PROGRESS:
		++ag->n_progress;

		break;

	case BEVENT_CALL_ESTABLISHED:
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

	case BEVENT_CALL_CLOSED:
		++ag->n_closed;

		ag->close_scode = call_scode(call);
		mem_deref(ag->close_prm);
		str_dup(&ag->close_prm, prm);

		if (ag->close_scode)
			ag->failed = true;

		if (ag->n_closed >= f->exp_closed &&
		    ag->peer->n_closed >= f->exp_closed) {

			re_cancel();
		}
		break;

	case BEVENT_CALL_TRANSFER:
		++ag->n_transfer;

		err = ua_call_alloc(&call2, ua, VIDMODE_ON, NULL, call,
				call_localuri(call), true);
		if (!err) {
			call_set_user_data(call2, call_user_data(call));

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

	case BEVENT_CALL_TRANSFER_FAILED:
		++ag->n_transfer_fail;

		call_hold(call, false);
		if (ua == f->a.ua)
			re_cancel();

		break;

	case BEVENT_CALL_REMOTE_SDP:
		if (!str_cmp(prm, "offer"))
			++ag->n_offer_cnt;
		else if (!str_cmp(prm, "answer"))
			++ag->n_answer_cnt;

		break;

	case BEVENT_CALL_HOLD:
		++ag->n_hold_cnt;
		break;

	case BEVENT_CALL_RESUME:
		++ag->n_resume_cnt;
		break;

	case BEVENT_CALL_MENC:
		++ag->n_mediaenc;

		if (strstr(prm, "audio"))
			strm = audio_strm(call_audio(call));
		else if (strstr(prm, "video"))
			strm = video_strm(call_video(call));

		if (strm) {
			ASSERT_TRUE(stream_is_secure(strm));
		}
		break;

	case BEVENT_CALL_DTMF_START:
		ASSERT_EQ(1, str_len(prm));
		ASSERT_EQ(f->dtmf_digits[ag->n_dtmf_recv], prm[0]);
		++ag->n_dtmf_recv;
		break;

	case BEVENT_CALL_DTMF_END:
		if (str_len(f->dtmf_digits) == ag->n_dtmf_recv)
			tmr_start(&ag->tmr, 0, delayed_dtmf_check, ag->peer);
		break;

	case BEVENT_CALL_RTPESTAB:
		++ag->n_rtpestab;

		if (strstr(prm, "audio"))
			++ag->n_audio_estab;
		else if (strstr(prm, "video"))
			++ag->n_video_estab;

		break;

	case BEVENT_CALL_RTCP:
		++ag->n_rtcp;

		break;

	default:
		break;
	}

	if (ag->failed && ag->peer->failed) {
		info("test: re_cancel on call failed\n");
		re_cancel();
		return;
	}

	process_rules(ag, ev, prm);

 out:
	if (err) {
		warning("error in event-handler (%m)\n", err);
		fixture_abort(f, err);
	}
}


int fixture_init_priv(struct fixture *f, const char *prm)
{
	int err = 0;
	char *buf = NULL;
	memset(f, 0, sizeof(*f));

	f->a.fix = f;
	f->b.fix = f;
	f->c.fix = f;
	err = sa_set_str(&f->dst, "127.0.0.1", 5060);
	TEST_ERR(err);

	err = ua_init("test", true, true, true);
	TEST_ERR(err);

	f->magic = MAGIC;
	f->estab_action = ACTION_RECANCEL;
	f->exp_estab = 1;
	f->exp_closed = 1;
	/* NOTE: See Makefile TEST_MODULES */
	err = module_load(".", "g711");
	TEST_ERR(err);

	err = re_sdprintf(&buf, "A <sip:a@127.0.0.1>;regint=0%s", prm);
	TEST_ERR(err);
	err = ua_alloc(&f->a.ua, buf);
	TEST_ERR(err);
	buf = mem_deref(buf);
	err = re_sdprintf(&buf, "B <sip:b@127.0.0.1>;regint=0%s", prm);
	TEST_ERR(err);
	err = ua_alloc(&f->b.ua, buf);
	TEST_ERR(err);

	f->a.peer = &f->b;
	f->b.peer = &f->a;

	err = bevent_register(event_handler, f);
	TEST_ERR(err);

	err = sip_transp_laddr(uag_sip(), &f->laddr_udp,
			       SIP_TRANSP_UDP, &f->dst);
	TEST_ERR(err);

	err = sip_transp_laddr(uag_sip(), &f->laddr_tcp,
			       SIP_TRANSP_TCP, &f->dst);
	TEST_ERR(err);

	err = sip_transp_laddr(uag_sip(), &f->laddr_tls,
			       SIP_TRANSP_TLS, &f->dst);
	TEST_ERR(err);

	debug("test: local SIP transp: UDP=%J, TCP=%J\n",
	      &f->laddr_udp, &f->laddr_tcp);

	re_snprintf(f->buri, sizeof(f->buri),
		    "sip:b@%J", &f->laddr_udp);
	re_snprintf(f->buri_tcp, sizeof(f->buri_tcp),
		    "sip:b@%J;transport=tcp", &f->laddr_tcp);
out:
	mem_deref(buf);
	return err;
}


void fixture_close(struct fixture *f)
{
	tmr_cancel(&f->a.tmr_ack);
	tmr_cancel(&f->b.tmr_ack);
	tmr_cancel(&f->c.tmr_ack);
	tmr_cancel(&f->a.tmr);
	tmr_cancel(&f->b.tmr);
	tmr_cancel(&f->c.tmr);
	mem_deref(f->command);
	mem_deref(f->c.ua);
	mem_deref(f->b.ua);
	mem_deref(f->a.ua);
	mem_deref(f->c.close_prm);
	mem_deref(f->b.close_prm);
	mem_deref(f->a.close_prm);

	module_unload("g711");

	bevent_unregister(event_handler);

	ua_stop_all(true);
	ua_close();
	list_flush(&f->rules);
}


void fixture_abort(struct fixture *f, int err)
{
	f->err = err;
	re_cancel();
}


int fixture_auframe_handle(struct fixture *fix, struct auframe *af,
			   const char *dev,
			   struct agent **pag)
{
	struct agent *ag = NULL;
	struct ua *ua;
	int err = 0;
	(void)af;

	ASSERT_EQ(MAGIC, fix->magic);

	if (!str_cmp(dev, "a")) {
		ag = &fix->a;
	}
	else if (!str_cmp(dev, "b")) {
		ag = &fix->b;
	}
	else {
		warning("test: received audio frame - agent unclear\n");
		return EINVAL;
	}

	ua = ag->ua;
	/* Does auframe come from the decoder ? */
	if (!audio_rxaubuf_started(call_audio(ua_call(ua)))) {
		debug("test: [%s] no audio received from decoder yet\n",
		      account_aor(ua_account(ua)));
		err = ENOENT;
		goto out;
	}

	++ag->n_auframe;
	(void)audio_level_get(call_audio(ua_call(ua)), &ag->aulvl);

	bevent_ua_emit(BEVENT_CUSTOM, ua, "auframe %u", ag->n_auframe);

 out:
	if (err && err != ENOENT)
		fixture_abort(fix, err);
	else if (pag)
		*pag = ag;

	return err;
}


static int vprintf_null(const char *p, size_t size, void *arg)
{
	(void)p;
	(void)size;
	(void)arg;
	return 0;
}


static void delayed_command(void *arg)
{
	struct fixture *fix = arg;
	const char *cmd = fix->command;
	struct re_printf pf_null = {vprintf_null, 0};
	int err = 0;

	err = cmd_process_long(baresip_commands(),
			       cmd, str_len(cmd), &pf_null, NULL);
	fix->command = mem_deref(fix->command);
	if (err)
		fixture_abort(fix, err);
}


void fixture_delayed_command(struct fixture *f,
			     uint32_t delay_ms, const char *cmd)
{
	f->command = mem_deref(f->command);
	str_dup(&f->command, cmd);
	tmr_start(&f->a.tmr, delay_ms, delayed_command, f);
}
