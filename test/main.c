/**
 * @file test/main.c  Selftest for Baresip core
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#ifdef HAVE_GETOPT
#include <getopt.h>
#endif
#include <re.h>
#include <baresip.h>
#include "test.h"

enum { ASYNC_WORKERS = 4 };

typedef int (test_exec_h)(void);

struct test {
	test_exec_h *exec;
	const char *name;
};

#define TEST(a) {a, #a}

static const struct test tests[] = {
	TEST(test_account),
	TEST(test_account_uri_complete),
	TEST(test_call_answer),
	TEST(test_call_answer_hangup_a),
	TEST(test_call_answer_hangup_b),
	TEST(test_call_aulevel),
	TEST(test_call_custom_headers),
	TEST(test_call_dtmf),
	TEST(test_call_format_float),
	TEST(test_call_max),
	TEST(test_call_mediaenc),
	TEST(test_call_medianat),
	TEST(test_call_multiple),
	TEST(test_call_progress),
	TEST(test_call_reject),
	TEST(test_call_cancel),
	TEST(test_call_rtcp),
	TEST(test_call_rtp_timeout),
	TEST(test_call_tcp),
	TEST(test_call_deny_udp),
	TEST(test_call_transfer),
	TEST(test_call_transfer_fail),
	TEST(test_call_attended_transfer),
	TEST(test_call_video),
	TEST(test_call_change_videodir),
	TEST(test_call_webrtc),
	TEST(test_call_bundle),
	TEST(test_call_ipv6ll),
	TEST(test_call_100rel_audio),
	TEST(test_call_100rel_video),
	TEST(test_call_hold_resume),
	TEST(test_call_srtp_tx_rekey),
	TEST(test_call_uag_find_msg),
#ifdef USE_TLS
	TEST(test_call_sni),
	TEST(test_call_cert_select),
#endif
	TEST(test_cmd),
	TEST(test_cmd_long),
	TEST(test_contact),
	TEST(test_bevent_register),
	TEST(test_jbuf),
	TEST(test_jbuf_adaptive),
	TEST(test_jbuf_video),
	TEST(test_jbuf_gnack),
	TEST(test_message),
	TEST(test_network),
	TEST(test_play),
	TEST(test_stunuri),
	TEST(test_ua_alloc),
	TEST(test_ua_cuser),
	TEST(test_ua_options),
	TEST(test_ua_refer),
	TEST(test_ua_register),
	TEST(test_ua_register_auth),
	TEST(test_ua_register_auth_dns),
	TEST(test_ua_register_dns),
	TEST(test_uag_find_param),
	TEST(test_video),
	TEST(test_clean_number),
	TEST(test_clean_number_only_numeric),
};


#ifdef DATA_PATH
static char datapath[256] = DATA_PATH;
#else
static char datapath[256] = "./test/data";
#endif


void test_set_datapath(const char *path)
{
	str_ncpy(datapath, path, sizeof(datapath));
}


const char *test_datapath(void)
{
	return datapath;
}


static int run_one_test(const struct test *test)
{
	struct config *config = conf_config();
	enum rtp_receive_mode rxmode = config->avt.rxmode;
	int err;

	re_printf("[ RUN      ] %s (rx %s)\n",
		  test->name, rtp_receive_mode_str(rxmode));

	err = test->exec();
	if (err) {
		warning("%s (rx %s): test failed (%m)\n",
			test->name, rtp_receive_mode_str(rxmode), err);
		return err;
	}

	re_printf("[       OK ]\n");

	return 0;
}


static int run_one_test_rxmode(const struct test *test, struct pl *rxmode)
{
	struct config *config = conf_config();
	int err;

	if (pl_isset(rxmode)) {
		config->avt.rxmode = resolve_receive_mode(rxmode);
		err = run_one_test(test);
		if (err)
			return err;
	}
	else {
		config->avt.rxmode = RECEIVE_MODE_MAIN;
		err = run_one_test(test);
		if (err)
			return err;

		config->avt.rxmode = RECEIVE_MODE_THREAD;
		err = run_one_test(test);
		if (err)
			return err;
	}

	return 0;
}


static int run_tests(void)
{
	size_t i;
	struct config *config = conf_config();
	enum rtp_receive_mode rxmode = config->avt.rxmode;
	int err;

	for (i=0; i<RE_ARRAY_SIZE(tests); i++) {

		re_printf("[ RUN      ] %s (rx %s)\n",
			  tests[i].name, rtp_receive_mode_str(rxmode));

		err = tests[i].exec();
		if (err) {
			warning("%s (rx %s): test failed (%m)\n",
				tests[i].name, rtp_receive_mode_str(rxmode),
				err);
			return err;
		}

		re_printf("[       OK ]\n");
	}

	return 0;
}


static int run_tests_rxmode(struct pl *rxmode)
{
	struct config *config = conf_config();
	int err;

	if (pl_isset(rxmode)) {
		config->avt.rxmode = resolve_receive_mode(rxmode);
		err = run_tests();
		if (err)
			return err;
	}
	else {
		config->avt.rxmode = RECEIVE_MODE_MAIN;
		err = run_tests();
		if (err)
			return err;

		config->avt.rxmode = RECEIVE_MODE_THREAD;
		err = run_tests();
		if (err)
			return err;
	}

	return 0;
}


static void test_listcases(void)
{
	size_t i, n;

	n = RE_ARRAY_SIZE(tests);

	(void)re_printf("\n%zu test cases:\n", n);

	for (i=0; i<(n+1)/2; i++) {

		(void)re_printf("    %-32s    %s\n",
				tests[i].name,
				(i+(n+1)/2) < n ? tests[i+(n+1)/2].name : "");
	}

	(void)re_printf("\n");
}


static const struct test *find_test(const char *name)
{
	size_t i;

	for (i=0; i<RE_ARRAY_SIZE(tests); i++) {

		if (0 == str_casecmp(name, tests[i].name))
			return &tests[i];
	}

	return NULL;
}


static void ua_exit_handler(void *arg)
{
	(void)arg;

	debug("ua exited -- stopping main runloop\n");
	re_cancel();
}


static void usage(void)
{
	(void)re_fprintf(stderr,
			 "Usage: selftest [options] <testcases..>\n"
			 "options:\n"
			 "\t-l               List all testcases and exit\n"
			 "\t-r <rxmode>      RTP RX processing mode "
			 "[main, thread]\n"
			 "\t-d <path>        Path to data files\n"
			 "\t-v               Verbose output (INFO level)\n"
			 );
}


static const char *modconfig =
	"ausrc_format    s16\n";


int main(int argc, char *argv[])
{
	struct memstat mstat;
	struct config *config;
	size_t ntests;
	struct sa sa;
	bool verbose = false;
	struct pl rxmode = PL_INIT;
	int err;

	libre_exception_btrace(true);

	err = libre_init();
	if (err)
		return err;

	log_enable_info(false);
	re_thread_async_init(ASYNC_WORKERS);

#ifdef HAVE_GETOPT
	for (;;) {
		const int c = getopt(argc, argv, "hlvr:d:");
		if (0 > c)
			break;

		switch (c) {

		case 'd':
			test_set_datapath(optarg);
			break;

		case '?':
		case 'h':
			usage();
			return -2;

		case 'l':
			test_listcases();
			return 0;

		case 'r':
			pl_set_str(&rxmode, optarg);
			break;

		case 'v':
			if (verbose)
				log_enable_debug(true);
			else
				log_enable_info(true);
			verbose = true;
			break;

		default:
			break;
		}
	}

	if (argc >= (optind + 1))
		ntests = argc - optind;
	else
		ntests = RE_ARRAY_SIZE(tests);
#else
	(void)argc;
	(void)argv;
	ntests = RE_ARRAY_SIZE(tests);
#endif

	re_printf("running baresip selftest version %s with %zu tests\n",
		  baresip_version(), ntests);

	err = conf_configure_buf((uint8_t *)modconfig, str_len(modconfig));
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	/* note: run SIP-traffic on localhost */
	config = conf_config();
	if (!config) {
		err = ENOENT;
		goto out;
	}

	err = baresip_init(config);
	err |= sa_set_str(&sa, "127.0.0.1", 0);
	err |= net_add_address(baresip_network(), &sa);
	if (err)
		goto out;

	str_ncpy(config->sip.local, "0.0.0.0:0", sizeof(config->sip.local));
	config->sip.verify_server = false;

	uag_set_exit_handler(ua_exit_handler, NULL);

#ifdef HAVE_GETOPT
	if (argc >= (optind + 1)) {

		for (size_t i=0; i<ntests; i++) {
			const char *name = argv[optind + i];
			const struct test *test;

			test = find_test(name);
			if (test) {
				err = run_one_test_rxmode(test, &rxmode);
				if (err)
					goto out;
			}
			else {
				re_fprintf(stderr,
					   "testcase not found: `%s'\n",
					   name);
				err = ENOENT;
				goto out;
			}
		}
	}
	else {
		err = run_tests_rxmode(&rxmode);
		if (err)
			goto out;
	}
#else
	err = run_tests_rxmode(&rxmode);
	if (err)
		goto out;
#endif

#if 1
	ua_stop_all(true);
#endif

	re_printf("\x1b[32mOK. %zu tests passed successfully\x1b[;m\n",
		  ntests);

 out:
	if (err) {
		warning("test failed (%m)\n", err);
		re_printf("%H\n", re_debug, NULL);
	}
	ua_stop_all(true);
	ua_close();
	conf_close();

	baresip_close();

	re_thread_async_close();

	tmr_debug();

	libre_close();

	mem_debug();

	if (0 == mem_get_stat(&mstat)) {
		if (mstat.bytes_cur || mstat.blocks_cur)
			return 2;
	}

	return err;
}
