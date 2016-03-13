/**
 * @file test/cplusplus.cpp  Baresip selftest -- C++ compatibility
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "test.h"


int test_cplusplus(void)
{
	const char *version = sys_libre_version_get();
	int err = 0;

	ASSERT_TRUE(str_isset(version));
	info("c++ ok\n");

out:
	return err;
}
