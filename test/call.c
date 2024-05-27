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
	BEHAVIOUR_NOTHING,
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


struct cancel_rule {
	struct le le;

	enum ua_event ev;
	const char *prm;
	struct ua *ua;
	bool checkack;

	unsigned n_incoming;
	unsigned n_progress;
	unsigned n_established;
	unsigned n_audio_estab;
	unsigned n_video_estab;
	unsigned n_offer_cnt;
	unsigned n_answer_cnt;
	unsigned n_vidframe;
	unsigned n_auframe;
	unsigned n_audebug;
	unsigned n_rtcp;
	double aulvl;

	struct cancel_rule *cr_and;
	bool met;
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
	unsigned n_offer_cnt;
	unsigned n_answer_cnt;
	unsigned n_hold_cnt;
	unsigned n_resume_cnt;
	unsigned n_vidframe;
	unsigned n_auframe;
	unsigned n_audebug;
	double aulvl;

	struct tmr tmr_ack;
	bool gotack;

	struct tmr tmr;
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
	struct list rules;
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
	tmr_cancel(&f->a.tmr_ack);		\
	tmr_cancel(&f->b.tmr_ack);		\
	tmr_cancel(&f->c.tmr_ack);		\
	tmr_cancel(&f->a.tmr);			\
	tmr_cancel(&f->b.tmr);			\
	tmr_cancel(&f->c.tmr);			\
	mem_deref(f->c.ua);			\
	mem_deref(f->b.ua);			\
	mem_deref(f->a.ua);			\
						\
	module_unload("g711");			\
						\
	uag_event_unregister(event_handler);	\
						\
	ua_stop_all(true);			\
	ua_close();				\
	list_flush(&f->rules);

#define fixture_abort(f, error)			\
	do {					\
		(f)->err = (error);		\
		re_cancel();			\
	} while (0)


static void cancel_rule_destructor(void *arg)
{
	struct cancel_rule *r = arg;

	list_unlink(&r->le);
	mem_deref(r->cr_and);
}


static struct cancel_rule *cancel_rule_alloc(enum ua_event ev,
					     struct ua *ua,
					     unsigned n_incoming,
					     unsigned n_progress,
					     unsigned n_established)
{
	struct cancel_rule *r = mem_zalloc(sizeof(*r), cancel_rule_destructor);
	if (!r)
		return NULL;

	r->ev = ev;
	r->ua = ua;
	r->n_incoming    = n_incoming;
	r->n_progress    = n_progress;
	r->n_established = n_established;

	r->n_audio_estab = (unsigned) -1;
	r->n_video_estab = (unsigned) -1;
	r->n_offer_cnt   = (unsigned) -1;
	r->n_answer_cnt  = (unsigned) -1;
	r->n_vidframe    = (unsigned) -1;
	r->n_auframe     = (unsigned) -1;
	r->n_audebug     = (unsigned) -1;
	r->n_rtcp        = (unsigned) -1;
	r->aulvl         = 0.0f;
	return r;
}


static struct cancel_rule *fixture_add_cancel_rule(struct fixture *f,
						   enum ua_event ev,
						   struct ua *ua,
						   unsigned n_incoming,
						   unsigned n_progress,
						   unsigned n_established)
{
	struct cancel_rule *r = cancel_rule_alloc(ev, ua, n_incoming,
						  n_progress, n_established);
	if (!r)
		return NULL;

	list_append(&f->rules, &r->le, r);
	return r;
}


static struct cancel_rule *cancel_rule_and_alloc(struct cancel_rule *cr,
						 enum ua_event ev,
						 struct ua *ua,
						 unsigned n_incoming,
						 unsigned n_progress,
						 unsigned n_established)
{
	struct cancel_rule *r = cancel_rule_alloc(ev, ua, n_incoming,
						  n_progress, n_established);
	if (!r)
		return NULL;

	cr->cr_and = r;
	return r;
}


#define UINTSET(u) (u) != (unsigned) -1

#define cr_debug_nbr(fld) \
	(UINTSET(cr->fld) ? \
		re_hprintf(pf, "    " #fld ":    %u\n", cr->fld) : 0)


static int cancel_rule_debug(struct re_printf *pf,
			     const struct cancel_rule *cr)
{
	int err;
	if (!cr)
		return 0;

	err  = re_hprintf(pf, "  --- %s ---\n", uag_event_str(cr->ev));
	err |= re_hprintf(pf, "    prm:  %s\n", cr->prm);
	err |= re_hprintf(pf, "    ua:   %s\n",
			  account_aor(ua_account(cr->ua)));
	err |= cr_debug_nbr(n_incoming);
	err |= cr_debug_nbr(n_progress);
	err |= cr_debug_nbr(n_established);
	err |= cr_debug_nbr(n_audio_estab);
	err |= cr_debug_nbr(n_video_estab);
	err |= cr_debug_nbr(n_offer_cnt);
	err |= cr_debug_nbr(n_answer_cnt);
	err |= cr_debug_nbr(n_auframe);
	err |= cr_debug_nbr(n_vidframe);
	err |= cr_debug_nbr(n_audebug);
	err |= cr_debug_nbr(n_rtcp);
	err |= re_hprintf(pf, "    met:  %s\n", cr->met ? "yes": "no");
	if (err)
		return err;

	if (cr->cr_and) {
		err |= re_hprintf(pf, "  AND -->\n");
		err |= cancel_rule_debug(pf, cr->cr_and);
	}

	return err;
}


#define ag_debug_nbr(fld) \
	re_hprintf(pf, "    " #fld ": %u\n", ag->fld)


static int agent_debug(struct re_printf *pf, const struct agent *ag)
{
	int err;
	if (!ag)
		return 0;

	char c = &ag->fix->a == ag ? 'a' :
		 &ag->fix->b == ag ? 'b' : 'c';
	err = re_hprintf(pf, "  --- Agent %c ---\n", c);
	err |= ag_debug_nbr(close_scode);
	err |= re_hprintf(pf, "    failed: %s\n", ag->failed ? "yes" : "no");
	err |= ag_debug_nbr(n_incoming);
	err |= ag_debug_nbr(n_progress);
	err |= ag_debug_nbr(n_established);
	err |= ag_debug_nbr(n_closed);
	err |= ag_debug_nbr(n_transfer_fail);
	err |= ag_debug_nbr(n_dtmf_recv);
	err |= ag_debug_nbr(n_transfer);
	err |= ag_debug_nbr(n_mediaenc);
	err |= ag_debug_nbr(n_rtpestab);
	err |= ag_debug_nbr(n_rtcp);
	err |= ag_debug_nbr(n_audio_estab);
	err |= ag_debug_nbr(n_video_estab);
	err |= ag_debug_nbr(n_offer_cnt);
	err |= ag_debug_nbr(n_answer_cnt);
	err |= ag_debug_nbr(n_hold_cnt);
	err |= ag_debug_nbr(n_resume_cnt);
	err |= ag_debug_nbr(n_auframe);
	err |= ag_debug_nbr(n_audebug);
	err |= ag_debug_nbr(n_vidframe);

	return err;
}


static void failure_debug(struct fixture *f, bool c)
{
	struct le *le;

	re_printf("Cancel Rules:\n");
	LIST_FOREACH(&f->rules, le) {
		struct cancel_rule *cr = le->data;

		re_printf("%H", cancel_rule_debug, cr);
	}

	re_printf("Agents:\n");
	re_printf("%H", agent_debug, &f->a);
	re_printf("%H", agent_debug, &f->b);
	if (c)
		re_printf("%H", agent_debug, &f->c);
}


static void cancel_rule_reset(struct cancel_rule *cr)
{
	cr->met = false;

	if (cr->cr_and)
		cancel_rule_reset(cr->cr_and);
}


static void cancel_rules_reset(struct fixture *f)
{
	struct le *le;

	LIST_FOREACH(&f->rules, le) {
		struct cancel_rule *cr = le->data;

		cancel_rule_reset(cr);
	}
}


#define cancel_rule_new(ev, ua, n_incoming, n_progress, n_established)    \
	cr = fixture_add_cancel_rule(f, ev, ua, n_incoming, n_progress,   \
				     n_established);			  \
	if (!cr) {							  \
		err = ENOMEM;						  \
		goto out;						  \
	}


#define cancel_rule_and(ev, ua, n_incoming, n_progress, n_established)	  \
	cr = cancel_rule_and_alloc(cr, ev, ua, n_incoming, n_progress,	  \
				   n_established);			  \
	if (!cr) {							  \
		err = ENOMEM;						  \
		goto out;						  \
	}


#define cancel_rule_pop()						  \
	mem_deref(list_tail(&f->rules)->data);


#define UINTSET(u) (u) != (unsigned) -1

static const struct list *hdrs;


static const char dtmf_digits[] = "123";


static void check_ack(void *arg)
{
	struct agent *ag = arg;

	if (ag->gotack)
		return;

	ag->gotack = !call_ack_pending(ua_call(ag->ua));

	if (ag->gotack)
		ua_event(ag->ua, UA_EVENT_CUSTOM, ua_call(ag->ua), "gotack");

	else
		tmr_start(&ag->tmr_ack, 1, check_ack, ag);
}


static int agent_wait_for_ack(struct agent *ag, unsigned n_incoming,
			      unsigned n_progress, unsigned n_established)
{
	int err;
	struct cancel_rule *cr;
	struct fixture *f = ag->fix;

	if (!call_ack_pending(ua_call(ag->ua)))
		return 0;

	cancel_rule_new(UA_EVENT_CUSTOM, ag->ua, n_incoming, n_progress,
			n_established);
	cr->prm = "gotack";
	cr->checkack = true;

	ag->gotack = false;
	tmr_start(&ag->tmr_ack, 1, check_ack, ag);
	err = re_main_timeout(10000);
	cancel_rule_pop();
	if (err)
		goto out;

	err = call_ack_pending(ua_call(ag->ua)) ? ETIMEDOUT : 0;

out:
	return err;
}


static bool check_rule(struct cancel_rule *rule, int met_prev,
		       struct agent *ag, enum ua_event ev, const char *prm)
{
	bool met_next = true;
	if (rule->cr_and) {
		met_next = check_rule(rule->cr_and, rule->met && met_prev, ag,
				      ev, prm);
		if (rule->met && met_prev && met_next)
			return true;
	}

	if (rule->met)
		goto out;

	if (ev != rule->ev)
		return false;

	if (str_isset(rule->prm) &&
	    !str_str(prm, rule->prm)) {
		info("test: event %s prm=[%s] (expected [%s])\n",
		     uag_event_str(ev), prm, rule->prm);
		return false;
	}

	if (rule->ua &&
	    ag->ua != rule->ua) {
		info("test: event %s ua=[%s] (expected [%s]\n",
		     uag_event_str(ev),
		     account_aor(ua_account(ag->ua)),
		     account_aor(ua_account(rule->ua)));
		return false;
	}

	if (rule->checkack && !ag->gotack) {
		info("test: event %s waiting for ACK\n", uag_event_str(ev));
		return false;
	}

	if (UINTSET(rule->n_incoming) &&
	    ag->n_incoming != rule->n_incoming) {
		info("test: event %s n_incoming=%u (expected %u)\n",
		     uag_event_str(ev),
		     ag->n_incoming, rule->n_incoming);
		return false;
	}

	if (UINTSET(rule->n_progress) &&
	    ag->n_progress < rule->n_progress) {
		info("test: event %s n_progress=%u (expected %u)\n",
		     uag_event_str(ev),
		     ag->n_progress, rule->n_progress);
		return false;
	}

	if (UINTSET(rule->n_established) &&
	    ag->n_established != rule->n_established) {
		info("test: event %s n_established=%u (expected %u)\n",
		     uag_event_str(ev),
		     ag->n_established, rule->n_established);
		return false;
	}

	if (UINTSET(rule->n_audio_estab) &&
	    ag->n_audio_estab != rule->n_audio_estab) {
		info("test: event %s n_audio_estab=%u (expected %u)\n",
		     uag_event_str(ev),
		     ag->n_audio_estab, rule->n_audio_estab);
		return false;
	}

	if (UINTSET(rule->n_video_estab) &&
	    ag->n_video_estab != rule->n_video_estab) {
		info("test: event %s n_video_estab=%u (expected %u)\n",
		     uag_event_str(ev),
		     ag->n_video_estab, rule->n_video_estab);
		return false;
	}

	if (UINTSET(rule->n_offer_cnt) &&
	    ag->n_offer_cnt != rule->n_offer_cnt) {
		info("test: event %s n_offer_cnt=%u (expected %u)\n",
		     uag_event_str(ev),
		     ag->n_offer_cnt, rule->n_offer_cnt);
		return false;
	}

	if (UINTSET(rule->n_answer_cnt) &&
	    ag->n_answer_cnt != rule->n_answer_cnt) {
		info("test: event %s n_answer_cnt=%u (expected %u)\n",
		     uag_event_str(ev),
		     ag->n_answer_cnt, rule->n_answer_cnt);
		return false;
	}

	if (UINTSET(rule->n_vidframe) &&
	    ag->n_vidframe < rule->n_vidframe)
		return false;

	if (UINTSET(rule->n_auframe) &&
	    ag->n_auframe < rule->n_auframe)
		return false;

	if (UINTSET(rule->n_audebug) &&
	    ag->n_audebug < rule->n_audebug)
		return false;

	if (UINTSET(rule->n_rtcp) &&
	    ag->n_rtcp < rule->n_rtcp)
		return false;

	if (rule->aulvl != 0.0f &&
	    (ag->aulvl < rule->aulvl || ag->aulvl >= 0.0f))
		return false;

	rule->met = true;
out:

	if (met_prev && met_next) {
		info("canceled by %H", cancel_rule_debug, rule);
		re_cancel();
		cancel_rules_reset(ag->fix);
	}

	return met_next;
}


static void process_rules(struct agent *ag, enum ua_event ev, const char *prm)
{
	struct fixture *f = ag->fix;
	struct le *le;

	LIST_FOREACH(&f->rules, le) {
		check_rule(le->data, true, ag, ev, prm);
	}
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
		if (!str_cmp(prm, "offer"))
			++ag->n_offer_cnt;
		else if (!str_cmp(prm, "answer"))
			++ag->n_answer_cnt;

		break;

	case UA_EVENT_CALL_HOLD:
		++ag->n_hold_cnt;
		break;

	case UA_EVENT_CALL_RESUME:
		++ag->n_resume_cnt;
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

		break;

	case UA_EVENT_CALL_RTCP:
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
	ua_event(ua, UA_EVENT_CUSTOM, ua_call(ua), "vidframe %u",
		 ag->n_vidframe);

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
	cancel_rule_new(UA_EVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr->prm = "vidframe";
	cr->n_vidframe = 3;
	cancel_rule_and(UA_EVENT_CUSTOM, f->a.ua, 0, 0, 1);
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
	cr_prog = cancel_rule_new(UA_EVENT_CALL_PROGRESS, f->a.ua, 0, 1, 0);

	cr_vidb = cancel_rule_new(UA_EVENT_CUSTOM, f->b.ua, 1, 0, 1);
	cr_vidb->prm = "vidframe";
	cr_vidb->n_vidframe = 3;
	cr_vida = cancel_rule_and(UA_EVENT_CUSTOM, f->a.ua, 0, 1, 1);
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

	cancel_rule_new(UA_EVENT_CALL_REMOTE_SDP, f->b.ua, 1, 0, 1);
	cr->prm = "offer";
	cancel_rule_and(UA_EVENT_CALL_REMOTE_SDP, f->a.ua, 0, 1, 1);
	cr->prm = "answer";

	/* Set video inactive */
	cr_vida->ev = UA_EVENT_MAX;
	cr_vidb->ev = UA_EVENT_MAX;
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
	cr_vida->ev = UA_EVENT_CUSTOM;
	cr_vidb->ev = UA_EVENT_CUSTOM;
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

	cancel_rule_new(UA_EVENT_CUSTOM, f->b.ua, 1, 0, 0);
	cr->prm = "vidframe";
	cr->n_vidframe = 3;
	cancel_rule_and(UA_EVENT_CUSTOM, f->a.ua, 0, 1, 0);
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
	cancel_rule_new(UA_EVENT_CALL_REMOTE_SDP, f->b.ua, 1, 0, 0);
	cr->prm = "offer";
	cancel_rule_and(UA_EVENT_CALL_REMOTE_SDP, f->a.ua, 0, 1, 0);
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
		return;
	}

	ua = ag->ua;
	/* Does auframe come from the decoder ? */
	if (!audio_rxaubuf_started(call_audio(ua_call(ua)))) {
		debug("test: [%s] no audio received from decoder yet\n",
		      account_aor(ua_account(ua)));
		return;
	}

	++ag->n_auframe;
	(void)audio_level_get(call_audio(ua_call(ua)), &ag->aulvl);

	ua_event(ua, UA_EVENT_CUSTOM, ua_call(ua), "auframe %u",
		 ag->n_auframe);

 out:
	if (err)
		fixture_abort(fix, err);
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

	cancel_rule_new(UA_EVENT_CUSTOM, f->a.ua, 0, 0, 1);
	cr->prm = "auframe";
	cr->aulvl = -96.0f;
	cancel_rule_and(UA_EVENT_CUSTOM, f->b.ua, 1, 0, 1);
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

	cancel_rule_new(UA_EVENT_CUSTOM, f->b.ua, 1, -1, 0);
	cr->prm = "auframe";
	cr->n_auframe = 3;
	cancel_rule_and(UA_EVENT_CUSTOM, f->a.ua, 0, 1, 0);
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
	cancel_rule_new(UA_EVENT_CALL_REMOTE_SDP, f->b.ua, 1, -1, 0);
	cr->prm = "offer";
	cancel_rule_and(UA_EVENT_CALL_REMOTE_SDP, f->a.ua, 0, 1, 0);
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
	cancel_rule_new(UA_EVENT_CALL_PROGRESS, f->a.ua, 0, 1, 0);

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

	cancel_rule_new(UA_EVENT_CUSTOM, f->a.ua, 0, 0, 1);
	cr->prm = "auframe";
	cr->n_auframe = 3;
	cancel_rule_and(UA_EVENT_CUSTOM, f->b.ua, 1, 0, 1);
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
	cancel_rule_new(UA_EVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cancel_rule_and(UA_EVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);

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
	cancel_rule_new(UA_EVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cancel_rule_and(UA_EVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);

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

	ua_event(ag->ua, UA_EVENT_CUSTOM, ua_call(ag->ua), "audebug %u",
		 ag->n_audebug);

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
	cancel_rule_new(UA_EVENT_CALL_ESTABLISHED, f->b.ua, 1, 0, 1);

	cancel_rule_new(UA_EVENT_CALL_RTCP, f->b.ua, 1, 0, 1);
	cr->n_rtcp = 5;
	cancel_rule_and(UA_EVENT_CALL_RTCP, f->a.ua, 0, 0, -1);
	cr->n_rtcp = 5;
	cancel_rule_and(UA_EVENT_CUSTOM,    f->b.ua, 1, 0, 1);
	cr->prm = "audebug";
	cr->n_audebug = 5;
	cancel_rule_and(UA_EVENT_CUSTOM,    f->a.ua, 0, 0, -1);
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
	cancel_rule_new(UA_EVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cr->n_audio_estab = cr->n_video_estab = 1;
	cancel_rule_and(UA_EVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);
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

	cancel_rule_new(UA_EVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cancel_rule_and(UA_EVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);

	f->estab_action = ACTION_NOTHING;
	f->behaviour = BEHAVIOUR_ANSWER;

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

	if (conf_config()->avt.rxmode == RECEIVE_MODE_THREAD)
		return 0;

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

	cancel_rule_new(UA_EVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
	cancel_rule_and(UA_EVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);

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
	cancel_rule_new(UA_EVENT_CALL_RTPESTAB, f->a.ua, 0, 0, 1);
	cr->n_audio_estab = 1;
	cancel_rule_and(UA_EVENT_CALL_RTPESTAB, f->b.ua, 1, 0, 1);
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

	cancel_rule_new(UA_EVENT_CALL_REMOTE_SDP, f->b.ua, 1, 0, 1);
	cr->prm = "offer";
	cancel_rule_and(UA_EVENT_CALL_REMOTE_SDP, f->a.ua, 0, 0, 1);
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
	cancel_rule_new(UA_EVENT_CALL_CLOSED, f->b.ua, 1, 0, 1);
	call_hangup(ua_call(f->a.ua), 0, NULL);
	tmr_start(&f->b.tmr_ack, 1, check_ack, &f->b);
	err = re_main_timeout(10000);
	TEST_ERR(err);


	/* New call from A -> B with sendonly offered */
	list_flush(&f->rules);
	cancel_rule_new(UA_EVENT_CALL_RTPESTAB, f->b.ua, 2, 0, 2);
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

	cancel_rule_new(UA_EVENT_CALL_REMOTE_SDP, f->b.ua, 2, 0, 2);
	cr->prm = "offer";
	cancel_rule_and(UA_EVENT_CALL_REMOTE_SDP, f->a.ua, 0, 0, 2);
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
	cancel_rule_new(UA_EVENT_CALL_REMOTE_SDP, f->a.ua, 0, 0, 2);
	cr->prm = "offer";
	cancel_rule_and(UA_EVENT_CALL_REMOTE_SDP, f->b.ua, 2, 0, 2);
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
	cancel_rule_new(UA_EVENT_CALL_ESTABLISHED, f->a.ua, 0, 0, 1);
	cancel_rule_and(UA_EVENT_CALL_ESTABLISHED, f->b.ua, 1, 0, 1);

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

	cancel_rule_new(UA_EVENT_CUSTOM, f->a.ua, 0, 0, 1);
	cr->prm = "auframe";
	cr->n_auframe = 10;
	cancel_rule_and(UA_EVENT_CUSTOM, f->b.ua, 1, 0, 1);
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
