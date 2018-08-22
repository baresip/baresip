/**
 * @file src/main.c  Main application code
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
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


static void ua_exit_handler(void *arg)
{
	(void)arg;
	debug("ua exited -- stopping main runloop\n");

	/* The main run-loop can be stopped now */
	re_cancel();
}


static void usage(void)
{
	(void)re_fprintf(stderr,
			 "Usage: baresip [options]\n"
			 "options:\n"
#if HAVE_INET6
			 "\t-6               Prefer IPv6\n"
#endif
			 "\t-d               Daemon\n"
			 "\t-e <commands>    Execute commands (repeat)\n"
			 "\t-f <path>        Config path\n"
			 "\t-m <module>      Pre-load modules (repeat)\n"
			 "\t-p <path>        Audio files\n"
			 "\t-h -?            Help\n"
			 "\t-t               Test and exit\n"
			 "\t-n <net_if>      Specify network interface\n"
			 "\t-u <parameters>  Extra UA parameters\n"
			 "\t-v               Verbose debug\n"
			 );
}


int main(int argc, char *argv[])
{
	bool prefer_ipv6 = false, run_daemon = false, test = false;
	const char *ua_eprm = NULL;
	const char *execmdv[16];
	const char *net_interface = NULL;
	const char *audio_path = NULL;
	const char *modv[16];
	size_t execmdc = 0;
	size_t modc = 0;
	size_t i;
	int err;

	/*
	 * turn off buffering on stdout
	 */
	setbuf(stdout, NULL);

	(void)re_fprintf(stdout, "baresip v%s"
			 " Copyright (C) 2010 - 2018"
			 " Alfred E. Heggestad et al.\n",
			 BARESIP_VERSION);

	(void)sys_coredump_set(true);

	err = libre_init();
	if (err)
		goto out;

#ifdef HAVE_GETOPT
	for (;;) {
		const int c = getopt(argc, argv, "6de:f:p:hun:vtm:");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		case 'h':
			usage();
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
			if (execmdc >= ARRAY_SIZE(execmdv)) {
				warning("max %zu commands\n",
					ARRAY_SIZE(execmdv));
				err = EINVAL;
				goto out;
			}
			execmdv[execmdc++] = optarg;
			break;

		case 'f':
			conf_path_set(optarg);
			break;

		case 'm':
			if (modc >= ARRAY_SIZE(modv)) {
				warning("max %zu modules\n",
					ARRAY_SIZE(modv));
				err = EINVAL;
				goto out;
			}
			modv[modc++] = optarg;
			break;

		case 'p':
			audio_path = optarg;
			break;

		case 't':
			test = true;
			break;

		case 'n':
			net_interface = optarg;
			break;

		case 'u':
			ua_eprm = optarg;
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

	err = conf_configure();
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	/*
	 * Set the network interface before initializing the config
	 */
	if (net_interface) {
		struct config *theconf = conf_config();

		str_ncpy(theconf->net.ifname, net_interface,
			 sizeof(theconf->net.ifname));
	}

	/*
	* Initialise the top-level baresip struct, must be
	* done AFTER configuration is complete.
	*/
	err = baresip_init(conf_config(), prefer_ipv6);
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

	/* NOTE: must be done after all arguments are processed */
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

	/* Initialise User Agents */
	err = ua_init("baresip v" BARESIP_VERSION " (" ARCH "/" OS ")",
		      true, true, true, prefer_ipv6);
	if (err)
		goto out;

	uag_set_exit_handler(ua_exit_handler, NULL);

	if (ua_eprm) {
		err = uag_set_extra_params(ua_eprm);
		if (err)
			goto out;
	}

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

		log_enable_stdout(false);
	}

	info("baresip is ready.\n");

	/* Execute any commands from input arguments */
	for (i=0; i<execmdc; i++) {
		ui_input_str(execmdv[i]);
	}

	/* Main loop */
	err = re_main(signal_handler);

 out:
	if (err)
		ua_stop_all(true);

	ua_close();
	conf_close();

	baresip_close();

	/* NOTE: modules must be unloaded after all application
	 *       activity has stopped.
	 */
	debug("main: unloading modules..\n");
	mod_close();

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}
