/**
 * @file debug_cmd.c  Debug commands
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <stdlib.h>
#include <time.h>
#include <re.h>
#include <baresip.h>


static uint64_t start_ticks;          /**< Ticks when app started         */
static time_t start_time;             /**< Start time of application      */


static int cmd_net_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return net_debug(pf, baresip_network());
}


static int print_system_info(struct re_printf *pf, void *arg)
{
	uint32_t uptime;
	int err = 0;

	(void)arg;

	uptime = (uint32_t)((long long)(tmr_jiffies() - start_ticks)/1000);

	err |= re_hprintf(pf, "\n--- System info: ---\n");

	err |= re_hprintf(pf, " Machine:  %s/%s\n", sys_arch_get(),
			  sys_os_get());
	err |= re_hprintf(pf, " Version:  %s (libre v%s)\n",
			  BARESIP_VERSION, sys_libre_version_get());
	err |= re_hprintf(pf, " Build:    %H\n", sys_build_get, NULL);
	err |= re_hprintf(pf, " Kernel:   %H\n", sys_kernel_get, NULL);
	err |= re_hprintf(pf, " Uptime:   %H\n", fmt_human_time, &uptime);
	err |= re_hprintf(pf, " Started:  %s", ctime(&start_time));

#ifdef __VERSION__
	err |= re_hprintf(pf, " Compiler: %s\n", __VERSION__);
#endif

	return err;
}


static int cmd_config_print(struct re_printf *pf, void *unused)
{
	(void)unused;
	return config_print(pf, conf_config());
}


static int cmd_ua_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return ua_debug(pf, uag_current());
}


static int cmd_play_file(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	const char *filename = carg->prm;
	int err;

	err = re_hprintf(pf, "playing audio file \"%s\" ..\n", filename);
	if (err)
		return err;

	err = play_file(NULL, baresip_player(), filename, 0);
	if (err) {
		warning("debug_cmd: play_file(%s) failed (%m)\n",
			filename, err);
		return err;
	}

	return err;
}


static const struct cmd debugcmdv[] = {
{"main",     0,       0, "Main loop debug",          re_debug             },
{"config",  'g',      0, "Print configuration",      cmd_config_print     },
{"sipstat", 'i',      0, "SIP debug",                ua_print_sip_status  },
{"modules", 'm',      0, "Module debug",             mod_debug            },
{"netstat", 'n',      0, "Network debug",            cmd_net_debug        },
{"sysinfo", 's',      0, "System info",              print_system_info    },
{"timers",   0,       0, "Timer debug",              tmr_status           },
{"uastat",  'u',      0, "UA debug",                 cmd_ua_debug         },
{"memstat", 'y',      0, "Memory status",            mem_status           },
{"play",    0,  CMD_PRM, "Play audio file",          cmd_play_file        },
};


static int module_init(void)
{
	int err;

	start_ticks = tmr_jiffies();
	(void)time(&start_time);

	err = cmd_register(baresip_commands(),
			   debugcmdv, ARRAY_SIZE(debugcmdv));

	return err;
}


static int module_close(void)
{
	cmd_unregister(baresip_commands(), debugcmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(debug_cmd) = {
	"debug_cmd",
	"application",
	module_init,
	module_close
};
