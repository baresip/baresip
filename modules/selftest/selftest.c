/**
 * @file selftest.c  Selftest for Baresip core
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "selftest.h"


static void timeout_handler(void *arg)
{
	int *err = arg;

	warning("selftest: re_main() loop timed out -- test hung..\n");

	*err = ETIMEDOUT;

	re_cancel();
}


int re_main_timeout(uint32_t timeout)
{
	struct tmr tmr;
	int err = 0;

	tmr_init(&tmr);

	tmr_start(&tmr, timeout * 1000, timeout_handler, &err);
	re_main(NULL);

	tmr_cancel(&tmr);
	return err;
}


static int module_init(void)
{
	int err;

	err = test_cmd();
	if (err)
		return err;

	err = test_ua_alloc();
	if (err)
		return err;

	err = test_uag_find_param();
	if (err)
		return err;

	err = test_ua_register();
	if (err)
		return err;

	re_printf("\x1b[32mselftest passed successfully\x1b[;m\n");

	return 0;
}


static int module_close(void)
{
	return 0;
}


const struct mod_export DECL_EXPORTS(selftest) = {
	"selftest",
	"application",
	module_init,
	module_close
};
