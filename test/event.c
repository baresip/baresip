/**
 * @file test/event.c  Baresip selftest -- event handling
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


int test_event(void)
{
	struct odict *od = NULL;
	size_t i;
	int err = 0;

	static const enum ua_event eventv[] = {
		UA_EVENT_REGISTERING,
		UA_EVENT_REGISTER_OK,
		UA_EVENT_REGISTER_FAIL,
		UA_EVENT_UNREGISTERING,
		UA_EVENT_SHUTDOWN,
		UA_EVENT_EXIT
		/* .. more events .. */
	};

	for (i=0; i<ARRAY_SIZE(eventv); i++) {

		const enum ua_event ev = eventv[i];
		const struct odict_entry *entry;

		err = odict_alloc(&od, 8);
		ASSERT_EQ(0, err);

		err = event_encode_dict(od, NULL, ev, NULL, NULL);
		ASSERT_EQ(0, err);

		/* verify that something was added */
		ASSERT_TRUE(odict_count(od, false) >= 2);

		/* verify the mandatory entries */
		entry = odict_lookup(od, "type");
		ASSERT_TRUE(entry != NULL);
		ASSERT_EQ(ODICT_STRING, entry->type);
		ASSERT_STREQ(uag_event_str(ev), entry->u.str);

		od = mem_deref(od);
	}

 out:
	mem_deref(od);

	return err;
}
