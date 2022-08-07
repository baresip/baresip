/**
 * @file main.c Main application code
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */

#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <re.h>
#include <baresip.h>
#include "demo.h"


#define DEBUG_MODULE "baresip-webrtc"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static const char *modpath = "/usr/local/lib/baresip/modules";
static const char *server_cert = "/etc/demo.pem";
static const char *www_path = "www";


static const char *modv[] = {
	"ice",
	"dtls_srtp",

	/* audio */
	"opus",
	"g722",
	"g711",
	"ausine",

	/* video */
	"vp8",
	"avcodec",
	"vp9",
	"avformat",
	"sdl",
	"fakevideo"
};

static const char *ice_server = NULL;

static const char *modconfig =
	"opus_bitrate       96000\n"
	"opus_stereo        yes\n"
	"opus_sprop_stereo  yes\n"
	"\n"
	"avformat_pass_through  no\n"
	;


static void signal_handler(int signum)
{
	(void)signum;

	re_fprintf(stderr, "terminated on signal %d\n", signum);

	re_cancel();
}


static void usage(void)
{
	re_fprintf(stderr,
		   "Usage: baresip-webrtc [options]\n"
		   "\n"
		   "options:\n"
                   "\t-h               Help\n"
		   "\t-v               Verbose debug\n"
		   "\n"
		   "http:\n"
		   "\t-c <cert>        HTTP server certificate (%s)\n"
		   "\t-w <root>        HTTP server document root (%s)\n"
		   "\n"
		   "ice:\n"
		   "\t-i <server>      ICE server (%s)\n"
		   "\t-u <username>    ICE username\n"
		   "\t-p <password>    ICE password\n"
		   "\n",
		   server_cert,
		   www_path,
		   ice_server);
}


int main(int argc, char *argv[])
{
	struct config *config;
	const char *stun_user = NULL, *stun_pass = NULL;
	int err = 0;

	for (;;) {

		const int c = getopt(argc, argv, "c:hl:i:u:tvu:p:w:");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		default:
			err = EINVAL;
			/*@fallthrough@*/

		case 'c':
			server_cert = optarg;
			break;

		case 'h':
			usage();
			return err;

		case 'i':
			if (0 == str_casecmp(optarg, "null"))
				ice_server = NULL;
			else
				ice_server = optarg;
			break;

		case 'u':
			stun_user = optarg;
			break;

		case 'p':
			stun_pass = optarg;
			break;

		case 'v':
			log_enable_debug(true);
			break;

		case 'w':
			www_path = optarg;
			break;
		}
	}

	if (argc < 1 || (argc != (optind + 0))) {
		usage();
		return -2;
	}

	err = libre_init();
	if (err) {
		re_fprintf(stderr, "libre_init: %m\n", err);
		goto out;
	}

	sys_coredump_set(true);

	err = conf_configure_buf((uint8_t *)modconfig, str_len(modconfig));
	if (err) {
		warning("main: configure failed: %m\n", err);
		goto out;
	}

	config = conf_config();

	config->net.use_linklocal = false;

	/*
	 * Initialise the top-level baresip struct, must be
	 * done AFTER configuration is complete.
	 */
	err = baresip_init(conf_config());
	if (err) {
		warning("main: baresip init failed (%m)\n", err);
		goto out;
	}

	for (size_t i=0; i<ARRAY_SIZE(modv); i++) {

		err = module_load(modpath, modv[i]);
		if (err) {
			re_fprintf(stderr,
				   "could not pre-load module"
				   " '%s' (%m)\n", modv[i], err);
		}
	}

	str_ncpy(config->audio.src_mod, "ausine",
		 sizeof(config->audio.src_mod));
	str_ncpy(config->audio.src_dev,
		 "440",
		 sizeof(config->audio.src_dev));

	str_ncpy(config->video.src_mod, "avformat",
		 sizeof(config->video.src_mod));
	str_ncpy(config->video.src_dev, "lavfi,testsrc2",
		 sizeof(config->video.src_dev));

	config->audio.level = true;

	config->video.bitrate = 2000000;
	config->video.fps = 30.0;
	config->video.fullscreen = false;
	config->video.width  = 320;
	config->video.height = 240;

	/* override default config */
	config->avt.rtcp_mux = true;
	config->avt.rtp_stats = true;

	err = demo_init(server_cert, www_path,
			ice_server, stun_user, stun_pass);
	if (err) {
		re_fprintf(stderr, "failed to init demo: %m\n", err);
		goto out;
	}

	re_main(signal_handler);

	re_printf("Bye for now\n");

 out:
	demo_close();

	/* note: must be done before mod_close() */
	module_app_unload();

	conf_close();

	baresip_close();

	/* NOTE: modules must be unloaded after all application
	 *       activity has stopped.
	 */
	debug("main: unloading modules..\n");
	mod_close();

	tmr_debug();

	libre_close();

	/* Check for memory leaks */
	mem_debug();

	return err;
}
