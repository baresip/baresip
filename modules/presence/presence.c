/**
 * @file presence.c Presence module
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <re.h>
#include <baresip.h>
#include "presence.h"


static int status_update(struct ua *current_ua,
			 enum presence_status new_status)
{
	if (ua_presence_status(current_ua) == new_status)
		return 0;

	info("presence: update status of '%s' from '%s' to '%s'\n",
	     account_aor(ua_account(current_ua)),
	     contact_presence_str(ua_presence_status(current_ua)),
	     contact_presence_str(new_status));

	ua_presence_status_set(current_ua, new_status);

	publisher_update_status(current_ua);
	notifier_update_status(current_ua);

	return 0;
}


static int cmd_pres(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	enum presence_status new_status;
	struct le *le;

	if (0 == str_casecmp(carg->prm, "online"))
		new_status = PRESENCE_OPEN;
	else if (0 == str_casecmp(carg->prm, "offline"))
		new_status = PRESENCE_CLOSED;
	else
		return re_hprintf(pf, "usage: /presence online|offline\n");

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;

		status_update(ua, new_status);
	}

	return 0;
}


static const struct cmd cmdv[] = {
	{"presence", 0, CMD_PRM, "Set presence <online|offline>", cmd_pres },
};


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	(void)arg;

	if (ev == BEVENT_SHUTDOWN) {
		struct ua *ua = bevent_get_ua(event);
		debug("presence: ua=%p got event %d (%s)\n", ua, ev,
		      bevent_str(ev));

		publisher_close();
		notifier_close();
		subscriber_close_all();
	}
}


static int module_init(void)
{
	int err;

	err = subscriber_init();
	if (err)
		return err;

	err = publisher_init();
	if (err)
		return err;

	err = notifier_init();
	if (err)
		return err;

	err = cmd_register(baresip_commands(), cmdv, RE_ARRAY_SIZE(cmdv));
	if (err)
		return err;

	err = bevent_register(event_handler, NULL);
	if (err)
		return err;

	return 0;
}


static int module_close(void)
{
	bevent_unregister(event_handler);

	cmd_unregister(baresip_commands(), cmdv);

	publisher_close();

	notifier_close();

	subscriber_close();

	return 0;
}


const struct mod_export DECL_EXPORTS(presence) = {
	"presence",
	"application",
	module_init,
	module_close
};
