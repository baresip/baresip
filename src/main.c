/**
 * @file src/main.c  Main application code
 *
 * Copyright (C) 2010 - 2021 Alfred E. Heggestad
 */
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT
#include <getopt.h>
#endif
#include <re.h>
#include <baresip.h>

#define DEBUG_MODULE ""
#define DEBUG_LEVEL 0
#include <re_dbg.h>

enum { ASYNC_WORKERS = 4 };

static void signal_handler(int sig)
{
	static bool term = false;

	if (term) {
		module_app_unload();
		mod_close();
		exit(0);
	}

	term = true;

	info("terminated by signal %d\n", sig);

	ua_stop_all(false);
}


static void ua_exit_handler(void *arg)
{
	(void)arg;
	debug("ua exited -- stopping main runloop\n");

	/* The main run-loop can be stopped now */
	re_cancel();
}


static void tmr_quit_handler(void *arg)
{
	(void)arg;

	ua_stop_all(false);
}


#ifdef HAVE_GETOPT
static void usage(void)
{
	(void)re_fprintf(stderr,
			 "Usage: baresip [options]\n"
			 "options:\n"
			 "\t-4               Force IPv4 only\n"
			 "\t-6               Force IPv6 only\n"
			 "\t-a <software>    Specify SIP User-Agent string\n"
			 "\t-d               Daemon\n"
			 "\t-e <commands>    Execute commands (repeat)\n"
			 "\t-f <path>        Config path\n"
			 "\t-m <module>      Pre-load modules (repeat)\n"
			 "\t-p <path>        Audio files\n"
			 "\t-h -?            Help\n"
			 "\t-s               Enable SIP trace\n"
			 "\t-t <sec>         Quit after <sec> seconds\n"
			 "\t-n <net_if>      Specify network interface\n"
			 "\t-u <parameters>  Extra UA parameters\n"
			 "\t-v               Verbose debug\n"
			 "\t-T               Enable timestamps log\n"
			 "\t-c               Disable colored log\n"
			 );
}
#endif


int main(int argc, char *argv[])
{
	int af = AF_UNSPEC, run_daemon = false;
	const char *ua_eprm = NULL;
	const char *software =
		"baresip v" BARESIP_VERSION " (" ARCH "/" OS ")";
	const char *execmdv[16];
	const char *net_interface = NULL;
	const char *audio_path = NULL;
	const char *modv[16];
	struct tmr tmr_quit;
	bool sip_trace = false;
	size_t execmdc = 0;
	size_t modc = 0;
	size_t i;
	uint32_t tmo = 0;
	int dbg_level = DBG_INFO;
	enum dbg_flags dbg_flags = DBG_ANSI;

	int err;

	/*
	 * turn off buffering on stdout
	 */
	setbuf(stdout, NULL);

	(void)re_fprintf(stdout, "baresip v%s"
			 " Copyright (C) 2010 - 2025"
			 " Alfred E. Heggestad et al.\n",
			 baresip_version());

	(void)sys_coredump_set(true);

	err = libre_init();
	if (err)
		goto out;

#ifdef RE_TRACE_ENABLED
	err = re_trace_init("re_trace.json");
	if (err)
		goto out;
#endif

	tmr_init(&tmr_quit);

#ifdef HAVE_GETOPT
	for (;;) {
		const int c = getopt(argc, argv, "46a:de:f:p:hu:n:vst:m:Tc");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		case 'h':
			usage();
			return -2;

		case 'a':
			software = optarg;
			break;

		case '4':
			af = AF_INET;
			break;

		case '6':
			af = AF_INET6;
			break;

		case 'd':
			run_daemon = true;
			break;

		case 'e':
			if (execmdc >= RE_ARRAY_SIZE(execmdv)) {
				warning("max %zu commands\n",
					RE_ARRAY_SIZE(execmdv));
				err = EINVAL;
				goto out;
			}
			execmdv[execmdc++] = optarg;
			break;

		case 'f':
			conf_path_set(optarg);
			break;

		case 'm':
			if (modc >= RE_ARRAY_SIZE(modv)) {
				warning("max %zu modules\n",
					RE_ARRAY_SIZE(modv));
				err = EINVAL;
				goto out;
			}
			modv[modc++] = optarg;
			break;

		case 'p':
			audio_path = optarg;
			break;

		case 's':
			sip_trace = true;
			break;

		case 't':
			tmo = atoi(optarg);
			break;

		case 'n':
			net_interface = optarg;
			break;

		case 'u':
			ua_eprm = optarg;
			break;

		case 'v':
			log_enable_debug(true);
			dbg_level = DBG_DEBUG;
			break;

		case 'T':
			log_enable_timestamps(true);
			dbg_flags |= DBG_TIME;
			break;

		case 'c':
			log_enable_color(false);
			dbg_flags &= ~DBG_ANSI;
			break;

		default:
			break;
		}
	}
#else
	(void)argc;
	(void)argv;
#endif

	dbg_init(dbg_level, dbg_flags);
	err = conf_configure();
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	re_thread_async_init(ASYNC_WORKERS);

	/*
	 * Set the network interface before initializing the config
	 */
	if (net_interface) {
		struct config *theconf = conf_config();

		str_ncpy(theconf->net.ifname, net_interface,
			 sizeof(theconf->net.ifname));
	}

	/*
	 * Set prefer_ipv6 preferring the one given in -6 argument (if any)
	 */
	if (af != AF_UNSPEC)
		conf_config()->net.af = af;

	/*
	 * Initialise the top-level baresip struct, must be
	 * done AFTER configuration is complete.
	*/
	err = baresip_init(conf_config());
	if (err) {
		warning("main: baresip init failed (%m)\n", err);
		goto out;
	}

	/* Set audio path preferring the one given in -p argument (if any) */
	if (audio_path)
		play_set_path(baresip_player(), audio_path);
	else if (str_isset(conf_config()->audio.audio_path)) {
		play_set_path(baresip_player(),
			      conf_config()->audio.audio_path);
	}

	/* Initialise User Agents */
	err = ua_init(software, true, true, true);
	if (err)
		goto out;

	/* NOTE: must be done after all arguments are processed and UA is
		initialized; some modules (eg, ctrl_tcp) can only be preloaded
		when the UA is available */
	if (modc) {

		info("pre-loading modules: %zu\n", modc);

		for (i=0; i<modc; i++) {

			err = module_preload(modv[i]);
			if (err) {
				re_fprintf(stderr,
					   "could not pre-load module"
					   " '%s' (%m)\n", modv[i], err);
			}
		}
	}

	uag_set_exit_handler(ua_exit_handler, NULL);

	if (ua_eprm) {
		err = uag_set_extra_params(ua_eprm);
		if (err)
			goto out;
	}

	if (sip_trace)
		uag_enable_sip_trace(true);

	/* Load modules */
	err = conf_modules();
	if (err)
		goto out;

	if (run_daemon) {
		err = sys_daemon();
		if (err)
			goto out;

		log_enable_stdout(false);
	}

	info("baresip is ready.\n");

	/* Execute any commands from input arguments */
	for (i=0; i<execmdc; i++) {
		ui_input_str(execmdv[i]);
	}

	if (tmo) {
		tmr_start(&tmr_quit, tmo * 1000, tmr_quit_handler, NULL);
	}

	/* Main loop */
	err = re_main(signal_handler);

 out:
	tmr_cancel(&tmr_quit);

	if (err)
		ua_stop_all(true);

	ua_close();

	/* note: must be done before mod_close() */
	module_app_unload();

	conf_close();

	baresip_close();

	/* NOTE: modules must be unloaded after all application
	 *       activity has stopped.
	 */
	debug("main: unloading modules..\n");
	mod_close();

	re_thread_async_close();

#ifdef RE_TRACE_ENABLED
	re_trace_close();
#endif

	/* Check for open timers */
	tmr_debug();

	libre_close();

	/* Check for memory leaks */
	mem_debug();

	return err;
}
