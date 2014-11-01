/**
 * @file presence.c Presence module
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "presence.h"

enum presence_status my_status = PRESENCE_OPEN;


static int cmd_online(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	if (my_status == PRESENCE_OPEN)
		return 0;

	info("presence: update my status from '%s' to '%s'\n",
	     contact_presence_str(my_status),
	     contact_presence_str(PRESENCE_OPEN));

	my_status = PRESENCE_OPEN;

	publisher_update_status();
	notifier_update_status();

	return 0;
}


static int cmd_offline(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	if (my_status == PRESENCE_CLOSED)
		return 0;

	info("presence: update my status from '%s' to '%s'\n",
	     contact_presence_str(my_status),
	     contact_presence_str(PRESENCE_CLOSED));

	my_status = PRESENCE_CLOSED;

	publisher_update_status();
	notifier_update_status();

	return 0;
}


static const struct cmd cmdv[] = {
	{'[', 0, "Set presence online",   cmd_online  },
	{']', 0, "Set presence offline",  cmd_offline },
};


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

	return cmd_register(cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	cmd_unregister(cmdv);

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
