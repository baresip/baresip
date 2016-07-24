/**
 * @file test/contact.c  Baresip selftest -- contacts
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"


int test_contact(void)
{
	struct contacts contacts;
	struct contact *c;
	const char *addr = "sip:neil@young.com";
	struct pl pl_addr;
	int err;

	err = contact_init(&contacts);
	ASSERT_EQ(0, err);

	/* Verify that we have no contacts */

	ASSERT_EQ(0, list_count(contact_list(&contacts)));

	c = contact_find(&contacts, "sip:null@void.com");
	ASSERT_TRUE(c == NULL);

	/* Add one contact, list should have one entry and
	   find should return the added contact */

	pl_set_str(&pl_addr, addr);
	err = contact_add(&contacts, &c, &pl_addr);
	ASSERT_EQ(0, err);
	ASSERT_TRUE(c != NULL);

	ASSERT_EQ(1, list_count(contact_list(&contacts)));

	c = contact_find(&contacts, addr);
	ASSERT_TRUE(c != NULL);

	ASSERT_STREQ(addr, contact_str(c));

	/* Delete 1 contact, verify that list is empty */

	mem_deref(c);

	ASSERT_EQ(0, list_count(contact_list(&contacts)));

 out:
	contact_close(&contacts);

	return err;
}
