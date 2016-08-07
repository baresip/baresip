/**
 * @file presence.c Presence module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "presence.h"


static int status_update(struct ua *current_ua,
			 const enum presence_status new_status)
{
	if (ua_presence_status(current_ua) == new_status)
		return 0;

	info("presence: update status of '%s' from '%s' to '%s'\n",
	     ua_aor(current_ua),
	     contact_presence_str(ua_presence_status(current_ua)),
	     contact_presence_str(new_status));

	ua_presence_status_set(current_ua, new_status);

	publisher_update_status(current_ua);
	notifier_update_status(current_ua);

	return 0;
}


static int cmd_online(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	return status_update(uag_current(), PRESENCE_OPEN);
}


static int cmd_offline(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	return status_update(uag_current(), PRESENCE_CLOSED);
}


static const struct cmd cmdv[] = {
	{"presence_online",  '[', 0, "Set presence online",   cmd_online  },
	{"presence_offline", ']', 0, "Set presence offline",  cmd_offline },
};


static void event_handler(struct ua *ua, enum ua_event ev,
			  struct call *call, const char *prm, void *arg)
{
	(void)call;
	(void)prm;
	(void)arg;

	debug("presence: ua=%p got event %d (%s)\n", ua, ev,
	      uag_event_str(ev));

	if (ev == UA_EVENT_SHUTDOWN) {

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

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	err = uag_event_register(event_handler, NULL);
	if (err)
		return err;

	return err;
}


static int module_close(void)
{
	uag_event_unregister(event_handler);

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
