/**
 * @file test/main.c  Selftest for Baresip core
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "test.h"


typedef int (test_exec_h)(void);

struct test {
	test_exec_h *exec;
	const char *name;
};

#define TEST(a) {a, #a}

static const struct test tests[] = {
	TEST(test_cmd),
	TEST(test_ua_alloc),
	TEST(test_uag_find_param),
	TEST(test_ua_register),
	TEST(test_cplusplus),
};


static int run_tests(bool verbose)
{
	size_t i;
	int err;

	for (i=0; i<ARRAY_SIZE(tests); i++) {

		if (verbose) {
			re_printf("test %u -- %s\n",
				  i, tests[i].name);
		}

		err = tests[i].exec();
		if (err) {
			warning("%s: test failed (%m)\n",
				tests[i].name, err);
			return err;
		}
	}

	return 0;
}


int main(void)
{
	struct config *config;
	int err;

	err = libre_init();
	if (err)
		return err;

	re_printf("running test version %s\n", BARESIP_VERSION);

	/* note: run SIP-traffic on localhost */
	config = conf_config();
	if (!config) {
		err = ENOENT;
		goto out;
	}
	str_ncpy(config->sip.local, "127.0.0.1:0", sizeof(config->sip.local));

	/* XXX: needed for ua tests */
	err = ua_init("test", true, true, false, false);
	if (err)
		goto out;

	err = run_tests(false);
	if (err)
		goto out;

#if 1
	ua_stop_all(false);
	err = re_main_timeout(5);
	if (err)
		goto out;
#endif

	re_printf("\x1b[32mOK. %zu tests passed successfully\x1b[;m\n",
		  ARRAY_SIZE(tests));

 out:
	if (err) {
		warning("test failed (%m)\n", err);
		re_printf("%H\n", re_debug, 0);
	}
	ua_stop_all(true);
	ua_close();

	libre_close();

	tmr_debug();
	mem_debug();

	return err;
}
