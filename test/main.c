/**
 * @file test/main.c  Selftest for Baresip core
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <getopt.h>
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
	TEST(test_call_answer),
	TEST(test_call_reject),
	TEST(test_call_af_mismatch),
};


static int run_tests(void)
{
	size_t i;
	int err;

	for (i=0; i<ARRAY_SIZE(tests); i++) {

		re_printf("[ RUN      ] %s\n", tests[i].name);

		err = tests[i].exec();
		if (err) {
			warning("%s: test failed (%m)\n",
				tests[i].name, err);
			return err;
		}

		re_printf("[       OK ]\n");
	}

	return 0;
}


static void test_listcases(void)
{
	size_t i, n;

	n = ARRAY_SIZE(tests);

	(void)re_printf("\n%zu test cases:\n", n);

	for (i=0; i<(n+1)/2; i++) {

		(void)re_printf("    %-32s    %s\n",
				tests[i].name,
				(i+(n+1)/2) < n ? tests[i+(n+1)/2].name : "");
	}

	(void)re_printf("\n");
}


static void usage(void)
{
	(void)re_fprintf(stderr,
			 "Usage: selftest [options]\n"
			 "options:\n"
			 "\t-l               List all testcases and exit\n"
			 "\t-v               Verbose output (INFO level)\n"
			 );
}


int main(int argc, char *argv[])
{
	struct config *config;
	int err;

	err = libre_init();
	if (err)
		return err;

	log_enable_info(false);

	for (;;) {
		const int c = getopt(argc, argv, "hlv");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		case 'h':
			usage();
			return -2;

		case 'l':
			test_listcases();
			return 0;

		case 'v':
			log_enable_info(true);
			break;

		default:
			break;
		}
	}

	re_printf("running baresip selftest version %s with %zu tests\n",
		  BARESIP_VERSION, ARRAY_SIZE(tests));

	/* note: run SIP-traffic on localhost */
	config = conf_config();
	if (!config) {
		err = ENOENT;
		goto out;
	}
	str_ncpy(config->sip.local, "127.0.0.1:0", sizeof(config->sip.local));

	/* XXX: needed for ua tests */
	err = ua_init("test", true, true, true, false);
	if (err)
		goto out;

	err = run_tests();
	if (err)
		goto out;

#if 1
	ua_stop_all(true);
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
