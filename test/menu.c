/**
 * @file test/menu.c  Baresip selftest -- menu
 *
 * Copyright (C) 2010 - 2017 Alfred E. Heggestad
 */

#include <re.h>
#include <baresip.h>
#include "test.h"
#include "../modules/menu/menu.h"
#include "string.h"


#define DEBUG_MODULE "menu"
#define DEBUG_LEVEL 5
#include <re_dbg.h>

void clean_number(char* str);

int test_clean_number(void)
{
	int err = 0;
	char dial_number[32];

	strcpy(dial_number, "1234567");
	clean_number(dial_number);
	ASSERT_STREQ("1234567", dial_number);

	strcpy(dial_number, "+12 34 56 78");
	clean_number(dial_number);
	ASSERT_STREQ("+12345678", dial_number);

	strcpy(dial_number, "(100) 500123");
	clean_number(dial_number);
	ASSERT_STREQ("100500123", dial_number);

	strcpy(dial_number, "0412/34 56 78");
	clean_number(dial_number);
	ASSERT_STREQ("0412345678", dial_number);

	strcpy(dial_number, "012/34.56.78");
	clean_number(dial_number);
	ASSERT_STREQ("012345678", dial_number);

	strcpy(dial_number, "+64-1-234-5678");
	clean_number(dial_number);
	ASSERT_STREQ("+6412345678", dial_number);

	strcpy(dial_number, "005(0)12345");
	clean_number(dial_number);
	ASSERT_STREQ("00512345", dial_number);

	strcpy(dial_number, "+5(0)12345");
	clean_number(dial_number);
	ASSERT_STREQ("+512345", dial_number);

	strcpy(dial_number, "05(0)12345");
	clean_number(dial_number);
	ASSERT_STREQ("05012345", dial_number);

out:
	return err;
}


int test_clean_number_only_numeric(void)
{
	int err = 0;
	char dial_number[32];

	strcpy(dial_number, "(100)test500123");
	clean_number(dial_number);
	ASSERT_STREQ("(100)test500123", dial_number);

	strcpy(dial_number, "@(100)500123");
	clean_number(dial_number);
	ASSERT_STREQ("@(100)500123", dial_number);

out:
	return err;
}
