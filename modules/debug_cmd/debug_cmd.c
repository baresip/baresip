/**
 * @file debug_cmd.c  Debug commands
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */
#include <stdlib.h>
#include <time.h>
#ifdef USE_OPENSSL
#include <openssl/crypto.h>
#endif
#include <re.h>
#include <baresip.h>


/**
 * @defgroup debug_cmd debug_cmd
 *
 * Advanced debug commands
 */


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

#ifdef USE_OPENSSL
	err |= re_hprintf(pf, " OpenSSL:  %s\n",
			  SSLeay_version(SSLEAY_VERSION));
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
	const struct ua *ua = uag_current();
	(void)unused;

	if (ua)
		return ua_debug(pf, ua);
	else
		return re_hprintf(pf, "(no user-agent)\n");
}


/**
 * Returns all the User-Agents and their general codec state.
 * Formatted as JSON, for use with TCP / MQTT API interface.
 * JSON object with 'cuser' as the key.
 *
 * @return All User-Agents available, NULL if none
 */
static int cmd_api_uastate(struct re_printf *pf, void *unused)
{
	struct odict *od = NULL;
	struct le *le;
	int err;
	(void)unused;

	err = odict_alloc(&od, 8);
	if (err)
		return err;

	for (le = list_head(uag_list()); le && !err; le = le->next) {
		const struct ua *ua = le->data;
		struct odict *odua;

		err = odict_alloc(&odua, 8);

		err |= ua_state_json_api(odua, ua);
		err |= odict_entry_add(od, ua_aor(ua), ODICT_OBJECT, odua);
		mem_deref(odua);
	}

	err |= json_encode_odict(pf, od);
	if (err)
		warning("debug: failed to encode json (%m)\n", err);

	mem_deref(od);

	return re_hprintf(pf, "\n");
}


static int cmd_play_file(struct re_printf *pf, void *arg)
{
	static struct play *g_play;
	struct cmd_arg *carg = arg;
	struct config *cfg;
	const char *filename = carg->prm;
	int err = 0;

	cfg = conf_config();

	/* Stop the current tone, if any */
	g_play = mem_deref(g_play);

	if (str_isset(filename))
	{
		err = re_hprintf(pf, "playing audio file \"%s\" ..\n",
				 filename);
		if (err)
			return err;

		err = play_file(&g_play, baresip_player(), filename, 0,
                        cfg->audio.alert_mod, cfg->audio.alert_dev);
		if (err)
		{
			warning("debug_cmd: play_file(%s) failed (%m)\n",
					filename, err);
			return err;
		}
	}

	return err;
}


static int cmd_sip_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return sip_debug(pf, uag_sip());
}


static int reload_config(struct re_printf *pf, void *arg)
{
	int err;
	(void)arg;

	err = re_hprintf(pf, "reloading config file ..\n");
	if (err)
		return err;

	err = conf_configure();
	if (err) {
		(void)re_hprintf(pf, "reload_config failed: %m\n", err);
		return err;
	}

	(void)re_hprintf(pf, "done\n");

	return 0;
}


static int cmd_log_level(struct re_printf *pf, void *unused)
{
	int level;
	(void)unused;

	level = log_level_get();

	--level;

	if (level < LEVEL_DEBUG)
		level = LEVEL_ERROR;

	log_level_set(level);

	return re_hprintf(pf, "Log level '%s'\n", log_level_name(level));
}


static int print_uuid(struct re_printf *pf, void *arg)
{
	struct config *cfg = conf_config();
	(void)arg;

	if (cfg)
		re_hprintf(pf, "UUID: %s\n", cfg->sip.uuid);
	return 0;
}


static const struct cmd debugcmdv[] = {
{"conf_reload", 0,       0, "Reload config file",     reload_config       },
{"config",      0,       0, "Print configuration",    cmd_config_print    },
{"loglevel",   'v',      0, "Log level toggle",       cmd_log_level       },
{"main",        0,       0, "Main loop debug",        re_debug            },
{"memstat",    'y',      0, "Memory status",          mem_status          },
{"modules",     0,       0, "Module debug",           mod_debug           },
{"netstat",    'n',      0, "Network debug",          cmd_net_debug       },
{"play",        0, CMD_PRM, "Play audio file",        cmd_play_file       },
{"sipstat",    'i',      0, "SIP debug",              cmd_sip_debug       },
{"sysinfo",    's',      0, "System info",            print_system_info   },
{"timers",      0,       0, "Timer debug",            tmr_status          },
{"uastat",     'u',      0, "UA debug",               cmd_ua_debug        },
{"uuid",        0,       0, "Print UUID",             print_uuid          },
{"apistate",    0,       0, "User Agent state",       cmd_api_uastate     },
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
