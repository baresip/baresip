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


static struct {
	uint32_t prio;            /**< Current account prio           */
	uint32_t maxprio;         /**< Maximum account prio           */
	bool ready;               /**< All UA registered flag         */
} sreg;


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
	struct le *le;
	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		uint32_t prio = account_prio(ua_account(ua));

		if (!account_regint(ua_account(ua)))
			continue;

		if (prio != sreg.prio)
			continue;

		err = ua_register(ua);
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

	while (check_registrations()) {
		inc_account_prio();

		info("serreg: Register %s fail -> prio %u.\n",
				ua_aor(ua), sreg.prio);
		if (!register_curprio())
			break;

		if (prio == sreg.prio) {
			/* found none */
			sreg.prio = (uint32_t) -1;
			break;
		}
	}
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	(void)prm;
	(void)arg;


	switch (ev) {


	case UA_EVENT_REGISTER_OK:
		sreg.prio = account_prio(ua_account(ua));
		check_registrations();
		break;

	case UA_EVENT_REGISTER_FAIL:
		/* did we already increment? */
		if (account_prio(ua_account(ua)) != sreg.prio)
			break;

		next_account(ua);
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

	err = uag_event_register(ua_event_handler, NULL);
	return err;
}


static int module_close(void)
{
	uag_event_unregister(ua_event_handler);

	return 0;
}


const struct mod_export DECL_EXPORTS(serreg) = {
	"serreg",
	"application",
	module_init,
	module_close
};
