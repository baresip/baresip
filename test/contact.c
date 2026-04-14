/**
 * @file test/contact.c  Baresip selftest -- contacts
 *
 * Copyright (C) 2010 - 2016 Alfred E. Heggestad
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"
#include "call.h"


int test_contact(void)
{
	struct contacts *contacts = NULL;
	struct contact *c;
	const char *addr = "Neil Young <sip:neil@young.com>";
	const char *uri = "sip:neil@young.com";
	struct pl pl_addr;
	int err;

	err = contact_init(&contacts);
	ASSERT_EQ(0, err);

	/* Verify that we have no contacts */

	ASSERT_EQ(0, list_count(contact_list(contacts)));

	c = contact_find(contacts, "sip:null@void.com");
	ASSERT_TRUE(c == NULL);

	/* Add one contact, list should have one entry and
	   find should return the added contact */

	pl_set_str(&pl_addr, addr);
	err = contact_add(contacts, &c, &pl_addr);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(c != NULL);

	ASSERT_EQ(1, list_count(contact_list(contacts)));

	c = contact_find(contacts, uri);
	ASSERT_TRUE(c != NULL);

	ASSERT_STREQ(addr, contact_str(c));
	ASSERT_STREQ(uri, contact_uri(c));

	/* Delete 1 contact, verify that list is empty */

	mem_deref(c);

	ASSERT_EQ(0, list_count(contact_list(contacts)));

 out:
	mem_deref(contacts);

	return err;
}


int test_contact_find_call(void)
{
	struct contacts *contacts = NULL;
	const char *cstr[] = {
		"B <sip:b@127.0.0.1>",
		"A <sip:a@127.0.0.1>"};
	struct contact *c;
	struct pl pl_addr;
	struct fixture fix, *f = &fix;
	int err;

	fixture_init(f);

	err = contact_init(&contacts);
	TEST_ERR(err);

	/* contact_find_smart() with peer_uri */
	for (size_t i = 0; i < RE_ARRAY_SIZE(cstr); i++) {
		pl_set_str(&pl_addr, cstr[i]);
		err = contact_add(contacts, &c, &pl_addr);
		TEST_ERR(err);
	}

	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);

	struct call *call = ua_call(f->b.ua);
	struct contact *contact = contact_find_call(contacts, call);
	ASSERT_TRUE(contact != NULL);
	ASSERT_STREQ(cstr[1], contact_str(contact));

	ua_hangup(f->a.ua, call, 0, 0);

	/* host only match */
	contact_remove(contacts, contact);
	ASSERT_EQ(1, list_count(contact_list(contacts)));

	mem_deref(f->a.ua);
	err = ua_alloc(&f->a.ua, "A <sip:a@localhost>;regint=0");
	TEST_ERR(err);

	pl_set_str(&pl_addr, "X <sip:x@localhost>");
	contact_add(contacts, &c, &pl_addr);

	err = ua_connect(f->a.ua, 0, NULL, f->buri, VIDMODE_ON);
	TEST_ERR(err);

	err = re_main_timeout(5000);
	TEST_ERR(err);

	call = ua_call(f->b.ua);
	contact = contact_find_call(contacts, call);
	ASSERT_TRUE(contact != NULL);
	ASSERT_STREQ(contact_str(c), contact_str(contact));
	ASSERT_STREQ("sip:x@localhost", contact_uri(contact));

	ua_hangup(f->a.ua, call, 0, 0);

out:
	mem_deref(contacts);
	fixture_close(f);

	return err;
}
