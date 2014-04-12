/**
 * @file selftest/cmd.c  Baresip selftest -- cmd
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "selftest.h"


static bool cmd_called;


static int cmd_test(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	int err = 0;
	(void)pf;

	ASSERT_EQ(false, cmd_called);

	ASSERT_EQ('@', carg->key);
	ASSERT_TRUE(NULL == carg->prm);
	ASSERT_EQ(true, carg->complete);

	cmd_called = true;

 out:
	return err;
}


static const struct cmd cmdv[] = {
	{'@',       0, "Test command",  cmd_test},
};


static int vprintf_null(const char *p, size_t size, void *arg)
{
	(void)p;
	(void)size;
	(void)arg;
	return 0;
}


static struct re_printf pf_null = {vprintf_null, 0};


int test_cmd(void)
{
	struct cmd_ctx *ctx = 0;
	int err = 0;

	cmd_called = false;

	err = cmd_register(cmdv, ARRAY_SIZE(cmdv));
	ASSERT_EQ(0, err);

	/* issue a different command */
	err = cmd_process(&ctx, 'h', &pf_null);
	ASSERT_EQ(0, err);
	ASSERT_EQ(false, cmd_called);

	/* issue our command, expect handler to be called */
	err = cmd_process(&ctx, '@', &pf_null);
	ASSERT_EQ(0, err);
	ASSERT_EQ(true, cmd_called);

	cmd_unregister(cmdv);

	/* verify that context was not created */
	ASSERT_TRUE(NULL == ctx);

 out:
	return err;
}
