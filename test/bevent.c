/**
 * @file test/bevent.c  Baresip selftest -- event handling
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


struct fixture {
	int cnt;

	enum ua_event expected_event;
};


int test_event_encode(void)
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

	for (i=0; i<RE_ARRAY_SIZE(eventv); i++) {

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
		ASSERT_EQ(ODICT_STRING, odict_entry_type(entry));
		ASSERT_STREQ(uag_event_str(ev), odict_entry_str(entry));

		od = mem_deref(od);
	}

 out:
	mem_deref(od);

	return err;
}


static void event_handler(enum ua_event ev, struct bevent *event, void *arg)
{
	struct fixture *f = arg;
	void *apparg = bevent_get_apparg(event);
	struct ua *ua = bevent_get_ua(event);
	struct call *call = bevent_get_call(event);
	const struct sip_msg *msg = bevent_get_msg(event);
	(void)ev;

	if (apparg && apparg != (void *) 0xdeadbeef) {
		bevent_set_error(event, EINVAL);
	}
	else if (ua && ua != (void *) 0xdeadbeef) {
		bevent_set_error(event, EINVAL);
	}
	else if (call && call != (void *) 0xdeadbeef) {
		bevent_set_error(event, EINVAL);
	}
	else if (msg && msg != (void *) 0xdeadbeef) {
		bevent_set_error(event, EINVAL);
	}
	else if (f->expected_event != bevent_get_type(event)) {
		bevent_set_error(event, EINVAL);
	}
	else
		++f->cnt;
}


int test_event_register(void)
{
	int err = 0;
	struct fixture f;

	memset(&f, 0, sizeof(f));

	err = bevent_register(event_handler, &f);
	TEST_ERR(err);

	f.expected_event = UA_EVENT_EXIT;
	err = bevent_app_emit(UA_EVENT_EXIT, (void *) 0xdeadbeef, "%s",
			      "details");
	TEST_ERR(err);

	err = bevent_app_emit(UA_EVENT_SHUTDOWN, (void *) 0xdeadbeef, "%s",
			      "details");
	ASSERT_EQ(EINVAL, err);

	f.expected_event = UA_EVENT_REGISTER_OK;
	err = bevent_ua_emit(UA_EVENT_REGISTER_OK, (struct ua *) 0xdeadbeef,
			     NULL);
	TEST_ERR(err);

	f.expected_event = UA_EVENT_CALL_INCOMING;
	err = bevent_call_emit(UA_EVENT_CALL_INCOMING, NULL, NULL);
	TEST_ERR(err);

	f.expected_event = UA_EVENT_SIPSESS_CONN;
	err = bevent_sip_msg_emit(UA_EVENT_SIPSESS_CONN,
				  (struct sip_msg *) 0xdeadbeef, NULL);
	TEST_ERR(err);


	ASSERT_EQ(4, f.cnt);
out:
	bevent_unregister(event_handler);
	return err;
}
