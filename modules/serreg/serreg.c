/**
 * @file serreg.c  Serial registration mode
 *
 * Copyright (C) 2020 Commend.com - c.spielberger@commend.com
 */
#include <stdlib.h>
#include <time.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup serreg serreg
 *
 * Serial registration mode
 *
 *     Accounts optionally have priorities (default prio=0). Accounts with prio
 *     0 are registered on startup. Prio 1 accounts are fallback accounts and
 *     are registered if all of the prio 0 accounts registrations fail. Prio 2
 *     accounts are fallback accounts second stage, and so on.
 *
 *     If a (re-)REGISTER fails, then switches to the next priority UA.
 *
 * Cisco mode
 *
 *     Additionally to the serial mode sends Cisco REGISTER keep-alives to
 *     not-registered UAs in order to poll their availability. This is only a
 *     name for a REGISTER with expires zero, thus a periodic un-REGISTER.
 *
 *     If a (re-)REGISTER with positive expires value fails, serreg switches to
 *     the next available UA.
 *
 *     If a UA with prio lower than the current becomes available again, serreg
 *     switches to the UA with the lower prio.
 */

enum {
	MIN_RESTART_DELAY = 31,
};

static struct {
	uint32_t prio;            /**< Current account prio           */
	uint32_t maxprio;         /**< Maximum account prio           */
	bool ready;               /**< All UA registered flag         */
	uint32_t sprio;           /**< Prio where we need a restart   */
	struct tmr tmr;           /**< Restart timer                  */
	int failc;                /**< Fail count                     */
} sreg;


static uint32_t failwait(uint32_t failc)
{
	uint32_t w;

	w = min(1800, (30 * (1<<min(failc, 6)))) * (500 + rand_u16() % 501);
	return w;
}


/**
 * @return true if all registrations with current prio failed.
 */
static bool check_registrations(void)
{
	struct le *le;
	uint32_t n = 0;
	uint32_t f = 0;
	uint32_t r = 0;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);
		uint32_t prio = account_prio(acc);

		if (!account_regint(acc))
			continue;

		if (prio > sreg.maxprio)
			sreg.maxprio = prio;

		if (prio == sreg.prio)
			++n;

		if (prio == sreg.prio && ua_regfailed(ua))
			++f;

		if (prio == sreg.prio && ua_isregistered(ua))
			++r;
	}

	debug("serreg: %s:%d n=%u f=%u r=%u\n", __func__, __LINE__, n, f, r);
	if (n == f)
		return true;

	if (f)
		return false;

	if (r < n)
		return false;

	if (sreg.ready)
		return false;

	/* We are ready */
	ui_output(baresip_uis(),
		  "\x1b[32m%s serreg: %u useragent%s with prio %u "
		  "registered successfully! \x1b[;m\n",
		  n==1 ? "" : "All",
		  n, n==1 ? "" : "s",
		  sreg.prio);

	sreg.ready = true;
	return false;
}


static int register_curprio(void)
{
	int err = EINVAL;
	int erc;
	struct le *le;
	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		uint32_t prio = account_prio(ua_account(ua));
		uint32_t fbregint = account_fbregint(ua_account(ua));

		if (!account_regint(ua_account(ua)))
			continue;

		if (prio != sreg.prio) {
			if (!fbregint)
				ua_stop_register(ua);

			continue;
		}

		if (!fbregint || !ua_regfailed(ua)) {
			erc = ua_register(ua);

			if (err)
				err = erc;
		}
	}

	return err;
}


static int fallback_update(void)
{
	int err = EINVAL;
	struct le *le;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		const struct account *acc = ua_account(ua);
		uint32_t prio = account_prio(acc);

		if (!account_regint(acc))
			continue;

		if (prio == sreg.prio)
			continue;

		err = ua_fallback(ua);
		if (err)
			warning("serreg: could not start fallback %s (%m)\n",
				account_aor(acc), err);
	}

	return err;
}


static void inc_account_prio(void)
{
	struct le *le;
	uint32_t p = (uint32_t) -1;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		uint32_t prio = account_prio(ua_account(ua));

		if (prio <= sreg.prio)
			continue;

		if (prio < p)
			p = prio;
	}

	sreg.prio = p;
	if (sreg.prio > sreg.maxprio)
		sreg.prio = 0;

	sreg.ready = false;
}


static void next_account(struct ua *ua)
{
	uint32_t prio = sreg.prio;

	if (sreg.sprio == (uint32_t) -1)
		sreg.sprio = prio;

	while (check_registrations()) {
		inc_account_prio();

		if (sreg.sprio == sreg.prio) {
			/* no UA with success */
			sreg.prio = (uint32_t) -1;
			break;
		}

		info("serreg: Register %s fail -> prio %u.\n",
		     account_aor(ua_account(ua)), sreg.prio);
		if (!register_curprio())
			break;

		if (prio == sreg.prio) {
			/* found none */
			sreg.prio = (uint32_t) -1;
			break;
		}

		if (prio == (uint32_t) -1)
			prio = sreg.prio;
	}
}


static void fallback_ok(struct ua *ua)
{
	const struct account *acc = ua_account(ua);
	uint32_t prio = account_prio(acc);

	debug("serreg: fallback prio %u ok %s.\n", prio, account_aor(acc));

	if (prio <= sreg.prio) {
		info("serreg: Fallback %s ok -> prio %u.\n",
		     account_aor(acc), prio);
		sreg.prio = prio;
		sreg.ready = false;
		if (!register_curprio())
			(void)fallback_update();
	}
}


static void restart(void *arg)
{
	struct le *le;
	(void) arg;

	sreg.sprio = (uint32_t) -1;
	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);
		uint32_t prio = account_prio(acc);
		uint32_t fbregint = account_fbregint(acc);
		int err;

		if (!account_regint(acc))
			continue;

		/* fbregint accounts don't need a restart */
		if (prio || fbregint)
			continue;

		debug("serreg: restart %s prio 0.\n", account_aor(acc));
		sreg.prio = 0;
		err = ua_register(ua);
		if (err) {
			tmr_start(&sreg.tmr, failwait(++sreg.failc),
				  restart, NULL);
			break;
		}
		else {
			sreg.failc = 0;
		}
	}
}


static uint32_t min_regint(void)
{
	struct le *le;
	uint32_t m = 0;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		struct account *acc = ua_account(ua);
		uint32_t prio = account_prio(acc);
		uint32_t regint = account_regint(acc);
		uint32_t fbregint = account_fbregint(acc);

		if (!account_regint(acc))
			continue;

		if (prio || fbregint)
			continue;

		if (!m || regint < m)
			m = regint;
	}

	if (m < MIN_RESTART_DELAY)
		m = MIN_RESTART_DELAY;

	return m;
}


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct ua *ua = bevent_get_ua(event);
	(void)arg;

	switch (ev) {

	case BEVENT_FALLBACK_FAIL:
		debug("serreg: fallback fail %s.\n",
		      account_aor(ua_account(ua)));
		break;

	case BEVENT_FALLBACK_OK:
		fallback_ok(ua);
		break;

	case BEVENT_REGISTER_OK:
		sreg.prio = account_prio(ua_account(ua));
		check_registrations();
		sreg.sprio = sreg.prio;
		break;

	case BEVENT_REGISTER_FAIL:
		next_account(ua);
		if (account_fbregint(ua_account(ua)))
			(void)ua_fallback(ua);

		if (sreg.prio == (uint32_t) -1)
			tmr_start(&sreg.tmr, min_regint()*1000, restart, NULL);
		break;

	default:
		break;
	}
}


static int module_init(void)
{
	int err;

	sreg.maxprio = 0;
	sreg.prio = 0;
	sreg.ready = false;
	sreg.sprio = (uint32_t) -1;

	err = bevent_register(event_handler, NULL);
	return err;
}


static int module_close(void)
{
	bevent_unregister(event_handler);
	tmr_cancel(&sreg.tmr);

	return 0;
}


const struct mod_export DECL_EXPORTS(serreg) = {
	"serreg",
	"application",
	module_init,
	module_close
};
