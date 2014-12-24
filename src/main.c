/**
 * @file main.c  Main application code
 *
 * Copyright (C) 2010 - 2011 Creytiv.com
 */
#ifdef SOLARIS
#define __EXTENSIONS__ 1
#endif
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT
#include <getopt.h>
#endif
#include <re.h>
#include <baresip.h>


static void signal_handler(int sig)
{
	static bool term = false;

	if (term) {
		mod_close();
		exit(0);
	}

	term = true;

	info("terminated by signal %d\n", sig);

	ua_stop_all(false);
}


int main(int argc, char *argv[])
{
	bool prefer_ipv6 = false, run_daemon = false, test = false;
	const char *exec = NULL;
	int err;

	(void)re_fprintf(stderr, "baresip v%s"
			 " Copyright (C) 2010 - 2014"
			 " Alfred E. Heggestad et al.\n",
			 BARESIP_VERSION);

	(void)sys_coredump_set(true);

#ifdef HAVE_GETOPT
	for (;;) {
		const int c = getopt(argc, argv, "6de:f:p:hvt");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		case 'h':
			(void)re_fprintf(stderr,
					 "Usage: baresip [options]\n"
					 "options:\n"
#if HAVE_INET6
					 "\t-6               Prefer IPv6\n"
#endif
					 "\t-d               Daemon\n"
					 "\t-e <commands>    Exec commands\n"
					 "\t-f <path>        Config path\n"
					 "\t-p <path>        Audio files\n"
					 "\t-h -?            Help\n"
					 "\t-t               Test and exit\n"
					 "\t-v               Verbose debug\n"
					 );
			return -2;

#if HAVE_INET6
		case '6':
			prefer_ipv6 = true;
			break;
#endif

		case 'd':
			run_daemon = true;
			break;

		case 'e':
			exec = optarg;
			break;

		case 'f':
			conf_path_set(optarg);
			break;

		case 'p':
			play_set_path(optarg);
			break;

		case 't':
			test = true;
			break;

		case 'v':
			log_enable_debug(true);
			break;

		default:
			break;
		}
	}
#else
	(void)argc;
	(void)argv;
#endif

	err = libre_init();
	if (err)
		goto out;

	err = conf_configure();
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	/* Initialise User Agents */
	err = ua_init("baresip v" BARESIP_VERSION " (" ARCH "/" OS ")",
		      true, true, true, prefer_ipv6);
	if (err)
		goto out;

	if (test)
		goto out;

	/* Load modules */
	err = conf_modules();
	if (err)
		goto out;

	if (run_daemon) {
		err = sys_daemon();
		if (err)
			goto out;

		log_enable_stderr(false);
	}

	info("baresip is ready.\n");

	if (exec)
		ui_input_str(exec);

	/* Main loop */
	err = re_main(signal_handler);

 out:
	if (err)
		ua_stop_all(true);

	ua_close();
	mod_close();

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}
