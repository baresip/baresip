/**
 * @file call_fixture.c  Call tests -- Cancel rules
 *
 * Copyright (C) 2026 Alfred E. Heggestad, Christian Spielberger
 */


#include <re.h>
#include <baresip.h>
#include "test.h"
#include "call.h"

#define DEBUG_MODULE "testcall"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


static void cancel_rule_destructor(void *arg)
{
	struct cancel_rule *r = arg;

	list_unlink(&r->le);
	mem_deref(r->cr_and);
}


static struct cancel_rule *cancel_rule_alloc(enum bevent_ev ev,
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
	r->n_closed      = (unsigned) -1;
	r->aulvl         = 0.0f;
	return r;
}


struct cancel_rule *fixture_add_cancel_rule(struct fixture *f,
					    enum bevent_ev ev,
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


struct cancel_rule *cancel_rule_and_alloc(struct cancel_rule *cr,
					  enum bevent_ev ev,
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

	err  = re_hprintf(pf, "  --- %s ---\n", bevent_str(cr->ev));
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
	err |= cr_debug_nbr(n_closed);
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


void failure_debug(struct fixture *f, bool c)
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

#define UINTSET(u) (u) != (unsigned) -1


void check_ack(void *arg)
{
	struct agent *ag = arg;

	if (ag->gotack)
		return;

	ag->gotack = !call_ack_pending(ua_call(ag->ua));

	if (ag->gotack)
		bevent_ua_emit(BEVENT_CUSTOM, ag->ua, "gotack");

	else
		tmr_start(&ag->tmr_ack, 1, check_ack, ag);
}


int agent_wait_for_ack(struct agent *ag, unsigned n_incoming,
		       unsigned n_progress, unsigned n_established)
{
	int err;
	struct cancel_rule *cr;
	struct fixture *f = ag->fix;

	if (!call_ack_pending(ua_call(ag->ua)))
		return 0;

	cancel_rule_new(BEVENT_CUSTOM, ag->ua, n_incoming, n_progress,
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
		       struct agent *ag, enum bevent_ev ev, const char *prm)
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
		     bevent_str(ev), prm, rule->prm);
		return false;
	}

	if (rule->ua &&
	    ag->ua != rule->ua) {
		info("test: event %s ua=[%s] (expected [%s]\n",
		     bevent_str(ev),
		     account_aor(ua_account(ag->ua)),
		     account_aor(ua_account(rule->ua)));
		return false;
	}

	if (rule->checkack && !ag->gotack) {
		info("test: event %s waiting for ACK\n", bevent_str(ev));
		return false;
	}

	if (UINTSET(rule->n_incoming) &&
	    ag->n_incoming != rule->n_incoming) {
		info("test: event %s n_incoming=%u (expected %u)\n",
		     bevent_str(ev),
		     ag->n_incoming, rule->n_incoming);
		return false;
	}

	if (UINTSET(rule->n_progress) &&
	    ag->n_progress < rule->n_progress) {
		info("test: event %s n_progress=%u (expected %u)\n",
		     bevent_str(ev),
		     ag->n_progress, rule->n_progress);
		return false;
	}

	if (UINTSET(rule->n_established) &&
	    ag->n_established != rule->n_established) {
		info("test: event %s n_established=%u (expected %u)\n",
		     bevent_str(ev),
		     ag->n_established, rule->n_established);
		return false;
	}

	if (UINTSET(rule->n_audio_estab) &&
	    ag->n_audio_estab != rule->n_audio_estab) {
		info("test: event %s n_audio_estab=%u (expected %u)\n",
		     bevent_str(ev),
		     ag->n_audio_estab, rule->n_audio_estab);
		return false;
	}

	if (UINTSET(rule->n_video_estab) &&
	    ag->n_video_estab != rule->n_video_estab) {
		info("test: event %s n_video_estab=%u (expected %u)\n",
		     bevent_str(ev),
		     ag->n_video_estab, rule->n_video_estab);
		return false;
	}

	if (UINTSET(rule->n_offer_cnt) &&
	    ag->n_offer_cnt != rule->n_offer_cnt) {
		info("test: event %s n_offer_cnt=%u (expected %u)\n",
		     bevent_str(ev),
		     ag->n_offer_cnt, rule->n_offer_cnt);
		return false;
	}

	if (UINTSET(rule->n_answer_cnt) &&
	    ag->n_answer_cnt != rule->n_answer_cnt) {
		info("test: event %s n_answer_cnt=%u (expected %u)\n",
		     bevent_str(ev),
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

	if (UINTSET(rule->n_closed) &&
	    ag->n_closed < rule->n_closed)
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


void process_rules(struct agent *ag, enum bevent_ev ev, const char *prm)
{
	struct fixture *f = ag->fix;
	struct le *le;

	LIST_FOREACH(&f->rules, le) {
		check_rule(le->data, true, ag, ev, prm);
	}
}


