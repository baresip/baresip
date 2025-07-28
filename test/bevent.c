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
	struct ua *ua;
	struct call *call;
	int cnt;

	enum bevent_ev expected_event;
};


static struct dummy {
	int foo;
} dummy;

static struct sip_msg *dummy_msg;


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct fixture *f = arg;
	void *apparg = bevent_get_apparg(event);
	struct ua *ua = bevent_get_ua(event);
	struct call *call = bevent_get_call(event);
	const struct sip_msg *msg = bevent_get_msg(event);
	const char *txt = bevent_get_text(event);

	if (apparg && apparg != &dummy)
		bevent_set_error(event, EINVAL);

	if (ua && ua != f->ua)
		bevent_set_error(event, EINVAL);

	if (call && call != f->call)
		bevent_set_error(event, EINVAL);

	if (msg && msg != dummy_msg)
		bevent_set_error(event, EINVAL);

	if (ev == BEVENT_MODULE &&
		 !!str_cmp(txt, "module,event,details"))
		bevent_set_error(event, EINVAL);


	if (f->expected_event != bevent_get_value(event))
		bevent_set_error(event, EINVAL);
	else
		++f->cnt;

	struct odict *od = NULL;
	int err = odict_alloc(&od, 8);
	err |= odict_encode_bevent(od, event);
	if (err) {
		warning("bevent: encode failed: %m\n", err);
		bevent_set_error(event, err);
		goto out;
	}

	/* verify that something was added */
	ASSERT_TRUE(odict_count(od, false) >= 2);

	/* verify the mandatory entries */
	const struct odict_entry *entry = odict_lookup(od, "type");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ(ODICT_STRING, odict_entry_type(entry));
	ASSERT_STREQ(bevent_str(ev), odict_entry_str(entry));

out:
	od = mem_deref(od);
	if (err)
		bevent_set_error(event, err);
}


static int sip_msg_readf(struct sip_msg **msgp, const char *file)
{
	char *fname;
	struct mbuf *mb = NULL;
	const char *dp = test_datapath();
	int err;

	err = re_sdprintf(&fname, "%s/sip/%s", dp, file);
	if (err)
		return err;

	err = fs_fread(&mb, fname);
	if (err)
		goto out;

	mb->pos = 0;
	err = sip_msg_decode(msgp, mb);
out:
	mem_deref(mb);
	mem_deref(fname);
	return err;
}


int test_bevent_register(void)
{
	int err = 0;
	struct fixture f={.cnt=0};

	err = ua_alloc(&f.ua, "A <sip:a@127.0.0.1>;regint=0");
	TEST_ERR(err);
	ua_call_alloc(&f.call, f.ua, VIDMODE_OFF, NULL, NULL, NULL, false);

	err = bevent_register(event_handler, &f);
	TEST_ERR(err);

	f.expected_event = BEVENT_EXIT;
	err = bevent_app_emit(BEVENT_EXIT, &dummy, "%s",
			      "details");
	TEST_ERR(err);

	err = bevent_app_emit(BEVENT_SHUTDOWN, &dummy, "%s",
			      "details");
	ASSERT_EQ(EINVAL, err);

	f.expected_event = BEVENT_REGISTER_OK;
	err = bevent_ua_emit(BEVENT_REGISTER_OK, f.ua, NULL);
	TEST_ERR(err);

	f.expected_event = BEVENT_CALL_INCOMING;
	err = bevent_call_emit(BEVENT_CALL_INCOMING, f.call, NULL);
	TEST_ERR(err);

	f.expected_event = BEVENT_SIPSESS_CONN;
	err = sip_msg_readf(&dummy_msg, "invite.sip");
	TEST_ERR(err);
	err = bevent_sip_msg_emit(BEVENT_SIPSESS_CONN,
				  dummy_msg, NULL);
	TEST_ERR(err);
	dummy_msg = mem_deref(dummy_msg);

	ASSERT_EQ(4, f.cnt);

out:
	bevent_unregister(event_handler);
	mem_deref(f.ua);
	return err;
}
