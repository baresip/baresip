/**
 * @file menu.c  Interactive menu
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <time.h>
#include <re.h>
#include <baresip.h>


/**
 * @defgroup menu menu
 *
 * Interactive menu
 *
 * This module must be loaded if you want to use the interactive menu
 * to control the Baresip application.
 */


/** Defines the status modes */
enum statmode {
	STATMODE_CALL = 0,
	STATMODE_OFF,
};


static uint64_t start_ticks;          /**< Ticks when app started         */
static time_t start_time;             /**< Start time of application      */
static struct tmr tmr_alert;          /**< Incoming call alert timer      */
static struct tmr tmr_stat;           /**< Call status timer              */
static enum statmode statmode;        /**< Status mode                    */
static struct mbuf *dialbuf;          /**< Buffer for dialled number      */
static struct le *le_cur;             /**< Current User-Agent (struct ua) */

static struct {
	struct play *play;
	bool bell;
} menu;


static void menu_set_incall(bool incall);
static void update_callstatus(void);
static void alert_stop(void);


static const char *translate_errorcode(uint16_t scode)
{
	switch (scode) {

	case 404: return "notfound.wav";
	case 486: return "busy.wav";
	case 487: return NULL; /* ignore */
	default:  return "error.wav";
	}
}


static void check_registrations(void)
{
	static bool ual_ready = false;
	struct le *le;
	uint32_t n;

	if (ual_ready)
		return;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;

		if (!ua_isregistered(ua))
			return;
	}

	n = list_count(uag_list());

	/* We are ready */
	ui_output("\x1b[32mAll %u useragent%s registered successfully!"
		  " (%u ms)\x1b[;m\n",
		  n, n==1 ? "" : "s",
		  (uint32_t)(tmr_jiffies() - start_ticks));

	ual_ready = true;
}


/**
 * Return the current User-Agent in focus
 *
 * @return Current User-Agent
 */
static struct ua *uag_cur(void)
{
	return uag_current();
}


/* Return TRUE if there are any active calls for any UAs */
static bool have_active_calls(void)
{
	struct le *le;

	for (le = list_head(uag_list()); le; le = le->next) {

		struct ua *ua = le->data;

		if (ua_call(ua))
			return true;
	}

	return false;
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


/**
 * Print the SIP Registration for all User-Agents
 *
 * @param pf     Print handler for debug output
 * @param unused Unused parameter
 *
 * @return 0 if success, otherwise errorcode
 */
static int ua_print_reg_status(struct re_printf *pf, void *unused)
{
	struct le *le;
	int err;

	(void)unused;

	err = re_hprintf(pf, "\n--- Useragents: %u ---\n",
			 list_count(uag_list()));

	for (le = list_head(uag_list()); le && !err; le = le->next) {
		const struct ua *ua = le->data;

		err  = re_hprintf(pf, "%s ", ua == uag_cur() ? ">" : " ");
		err |= ua_print_status(pf, ua);
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Print the current SIP Call status for the current User-Agent
 *
 * @param pf     Print handler for debug output
 * @param unused Unused parameter
 *
 * @return 0 if success, otherwise errorcode
 */
static int ua_print_call_status(struct re_printf *pf, void *unused)
{
	struct call *call;
	int err;

	(void)unused;

	call = ua_call(uag_cur());
	if (call) {
		err  = re_hprintf(pf, "\n%H\n", call_debug, call);
	}
	else {
		err  = re_hprintf(pf, "\n(no active calls)\n");
	}

	return err;
}


static int dial_handler(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	(void)pf;

	if (str_isset(carg->prm)) {

		mbuf_rewind(dialbuf);
		(void)mbuf_write_str(dialbuf, carg->prm);

		err = ua_connect(uag_cur(), NULL, NULL,
				 carg->prm, NULL, VIDMODE_ON);
	}
	else if (dialbuf->end > 0) {

		char *uri;

		dialbuf->pos = 0;
		err = mbuf_strdup(dialbuf, &uri, dialbuf->end);
		if (err)
			return err;

		err = ua_connect(uag_cur(), NULL, NULL, uri, NULL, VIDMODE_ON);

		mem_deref(uri);
	}

	if (err) {
		warning("menu: ua_connect failed: %m\n", err);
	}

	return err;
}


static void options_resp_handler(int err, const struct sip_msg *msg, void *arg)
{
	(void)arg;

	if (err) {
		warning("options reply error: %m\n", err);
		return;
	}

	if (msg->scode < 200)
		return;

	if (msg->scode < 300) {

		mbuf_set_pos(msg->mb, 0);
		info("----- OPTIONS of %r -----\n%b",
		     &(msg->to.auri), mbuf_buf(msg->mb),
		     mbuf_get_left(msg->mb));
		return;
	}

	info("%r: OPTIONS failed: %u %r\n", &(msg->to.auri),
	     msg->scode, &msg->reason);
}


static int options_command(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err = 0;

	(void)pf;

	if (str_isset(carg->prm)) {

		mbuf_rewind(dialbuf);
		(void)mbuf_write_str(dialbuf, carg->prm);

		err = ua_options_send(uag_cur(), carg->prm,
				      options_resp_handler, NULL);
	}
	else if (dialbuf->end > 0) {

		char *uri;

		dialbuf->pos = 0;
		err = mbuf_strdup(dialbuf, &uri, dialbuf->end);
		if (err)
			return err;

		err = ua_options_send(uag_cur(), uri,
				      options_resp_handler, NULL);

		mem_deref(uri);
	}

	if (err) {
		warning("menu: ua_options failed: %m\n", err);
	}

	return err;
}


static int cmd_answer(struct re_printf *pf, void *unused)
{
	struct ua *ua = uag_cur();
	int err;
	(void)unused;

	err = re_hprintf(pf, "%s: Answering incoming call\n", ua_aor(ua));

	/* Stop any ongoing ring-tones */
	menu.play = mem_deref(menu.play);

	ua_hold_answer(ua, NULL);

	return err;
}


static int cmd_hangup(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;

	/* Stop any ongoing ring-tones */
	menu.play = mem_deref(menu.play);
	alert_stop();

	ua_hangup(uag_cur(), NULL, 0, NULL);

	/* note: must be called after ua_hangup() */
	menu_set_incall(have_active_calls());

	return 0;
}


static int create_ua(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct le *le;
	int err = 0;

	(void)pf;

	 if (str_isset(carg->prm)) {

		mbuf_rewind(dialbuf);
		(void)mbuf_write_str(dialbuf, carg->prm);

		(void)re_hprintf(pf, "Creating UA for %s ...\n", carg->prm);
		err = ua_alloc(NULL, carg->prm);


	}
	else if (dialbuf->end > 0) {

		char *uri;

		dialbuf->pos = 0;
		err = mbuf_strdup(dialbuf, &uri, dialbuf->end);
		if (err)
			return err;

		(void)re_hprintf(pf, "Creating UA for %s ...\n", uri);
		err |=  ua_alloc(NULL, uri);

		mem_deref(uri);
	}

	for (le = list_head(uag_list()); le && !err; le = le->next) {
		const struct ua *ua = le->data;

		err  = re_hprintf(pf, "%s ", ua == uag_cur() ? ">" : " ");
		err |= ua_print_status(pf, ua);
	}

	err |= re_hprintf(pf, "\n");


	if (err) {
		(void)re_hprintf(pf, "menu: create_ua failed: %m\n", err);
	}


	return err;
}


static int cmd_ua_next(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;

	if (!le_cur)
		le_cur = list_head(uag_list());
	if (!le_cur)
		return 0;

	le_cur = le_cur->next ? le_cur->next : list_head(uag_list());

	(void)re_fprintf(stderr, "ua: %s\n", ua_aor(list_ledata(le_cur)));

	uag_current_set(list_ledata(le_cur));

	update_callstatus();

	return 0;
}


static int cmd_ua_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return ua_debug(pf, uag_cur());
}


static int cmd_print_calls(struct re_printf *pf, void *unused)
{
	(void)unused;
	return ua_print_calls(pf, uag_cur());
}


static int cmd_config_print(struct re_printf *pf, void *unused)
{
	(void)unused;
	return config_print(pf, conf_config());
}


static const struct cmd cmdv[] = {
	{'M',       0, "Main loop debug",          re_debug             },
	{'\n',      0, "Accept incoming call",     cmd_answer           },
	{'D',       0, "Accept incoming call",     cmd_answer           },
	{'b',       0, "Hangup call",              cmd_hangup           },
	{'c',       0, "Call status",              ua_print_call_status },
	{'d', CMD_PRM, "Dial",                     dial_handler         },
	{'h',       0, "Help menu",                cmd_print            },
	{'i',       0, "SIP debug",                ua_print_sip_status  },
	{'l',       0, "List active calls",        cmd_print_calls      },
	{'m',       0, "Module debug",             mod_debug            },
	{'n',       0, "Network debug",            net_debug            },
	{'o', CMD_PRM, "Options",                  options_command      },
	{'r',       0, "Registration info",        ua_print_reg_status  },
	{'s',       0, "System info",              print_system_info    },
	{'t',       0, "Timer debug",              tmr_status           },
	{'u',       0, "UA debug",                 cmd_ua_debug         },
	{'y',       0, "Memory status",            mem_status           },
	{0x1b,      0, "Hangup call",              cmd_hangup           },
	{' ',       0, "Toggle UAs",               cmd_ua_next          },
	{'T',       0, "Toggle UAs",               cmd_ua_next          },
	{'g',       0, "Print configuration",      cmd_config_print     },
	{'R', CMD_PRM, "Create User-Agent",        create_ua            },
	{'#', CMD_PRM, NULL,   dial_handler },
	{'*', CMD_PRM, NULL,   dial_handler },
	{'0', CMD_PRM, NULL,   dial_handler },
	{'1', CMD_PRM, NULL,   dial_handler },
	{'2', CMD_PRM, NULL,   dial_handler },
	{'3', CMD_PRM, NULL,   dial_handler },
	{'4', CMD_PRM, NULL,   dial_handler },
	{'5', CMD_PRM, NULL,   dial_handler },
	{'6', CMD_PRM, NULL,   dial_handler },
	{'7', CMD_PRM, NULL,   dial_handler },
	{'8', CMD_PRM, NULL,   dial_handler },
	{'9', CMD_PRM, NULL,   dial_handler },
};


static int call_audio_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return audio_debug(pf, call_audio(ua_call(uag_cur())));
}


static int call_audioenc_cycle(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;
	audio_encoder_cycle(call_audio(ua_call(uag_cur())));
	return 0;
}


static int call_reinvite(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;
	return call_modify(ua_call(uag_cur()));
}


static int call_mute(struct re_printf *pf, void *unused)
{
	struct audio *audio = call_audio(ua_call(uag_cur()));
	bool muted = !audio_ismuted(audio);
	(void)unused;

	(void)re_hprintf(pf, "\ncall %smuted\n", muted ? "" : "un-");
	audio_mute(audio, muted);

	return 0;
}


static int call_xfer(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	static bool xfer_inprogress;

	if (!xfer_inprogress && !carg->complete) {
		statmode = STATMODE_OFF;
		re_hprintf(pf, "\rPlease enter transfer target SIP uri:\n");
	}

	xfer_inprogress = true;

	if (carg->complete) {
		statmode = STATMODE_CALL;
		xfer_inprogress = false;
		return call_transfer(ua_call(uag_cur()), carg->prm);
	}

	return 0;
}


static int call_holdresume(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	(void)pf;

	return call_hold(ua_call(uag_cur()), 'x' == carg->key);
}


static int hold_prev_call(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	(void)pf;

	return call_hold(ua_prev_call(uag_cur()), 'H' == carg->key);
}


static int switch_audio_dev(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl_driver, pl_device;
	struct config_audio *aucfg;
	struct config *cfg;
	struct audio *a;
	struct le *le;
	char driver[16], device[128] = "";
	int err = 0;

	static bool switch_aud_inprogress;

	if (!switch_aud_inprogress && !carg->complete) {
		re_hprintf(pf,
			   "\rPlease enter audio device (driver,device)\n");
	}

	switch_aud_inprogress = true;

	if (carg->complete) {

		switch_aud_inprogress = false;

		if (re_regex(carg->prm, str_len(carg->prm), "[^,]+,[~]*",
			     &pl_driver, &pl_device)) {

			return re_hprintf(pf, "\rFormat should be:"
					  " driver,device\n");
		}

		pl_strcpy(&pl_driver, driver, sizeof(driver));
		pl_strcpy(&pl_device, device, sizeof(device));

		if (!ausrc_find(driver)) {
			re_hprintf(pf, "no such audio-source: %s\n", driver);
			return 0;
		}
		if (!auplay_find(driver)) {
			re_hprintf(pf, "no such audio-player: %s\n", driver);
			return 0;
		}

		re_hprintf(pf, "switch audio device: %s,%s\n",
			   driver, device);

		for (le = list_tail(ua_calls(uag_cur())); le; le = le->prev) {

			struct call *call = le->data;

			a = call_audio(call);

			err = audio_set_player(a, driver, device);
			if (err) {
				re_hprintf(pf, "failed to set audio-player"
					   " (%m)\n", err);
				break;
			}

			err = audio_set_source(a, driver, device);
			if (err) {
				re_hprintf(pf, "failed to set audio-source"
					   " (%m)\n", err);
				break;
			}
		}

		cfg = conf_config();
		if (!cfg) {
			return re_hprintf(pf, "no config object\n");
		}

		aucfg = &cfg->audio;

		str_ncpy(aucfg->play_mod, driver, sizeof(aucfg->play_mod));
		str_ncpy(aucfg->play_dev, device, sizeof(aucfg->play_dev));

		str_ncpy(aucfg->src_mod, driver, sizeof(aucfg->src_mod));
		str_ncpy(aucfg->src_dev, device, sizeof(aucfg->src_dev));

		str_ncpy(aucfg->alert_mod, driver, sizeof(aucfg->alert_mod));
		str_ncpy(aucfg->alert_dev, device, sizeof(aucfg->alert_dev));
	}

	return 0;
}


#ifdef USE_VIDEO
static int call_videoenc_cycle(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;
	video_encoder_cycle(call_video(ua_call(uag_cur())));
	return 0;
}


static int call_video_debug(struct re_printf *pf, void *unused)
{
	(void)unused;
	return video_debug(pf, call_video(ua_call(uag_cur())));
}
#endif


static int digit_handler(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct call *call;
	int err = 0;

	(void)pf;

	call = ua_call(uag_cur());
	if (call)
		err = call_send_digit(call, carg->key);

	return err;
}


static int toggle_statmode(struct re_printf *pf, void *arg)
{
	(void)pf;
	(void)arg;

	if (statmode == STATMODE_OFF)
		statmode = STATMODE_CALL;
	else
		statmode = STATMODE_OFF;

	return 0;
}


static const struct cmd callcmdv[] = {
	{'I',       0, "Send re-INVITE",      call_reinvite         },
	{'X',       0, "Call resume",         call_holdresume       },
	{'a',       0, "Audio stream",        call_audio_debug      },
	{'e',       0, "Cycle audio encoder", call_audioenc_cycle   },
	{'m',       0, "Call mute/un-mute",   call_mute             },
	{'r', CMD_IPRM,"Transfer call",       call_xfer             },
	{'x',       0, "Call hold",           call_holdresume       },
	{'H',       0, "Hold previous call",  hold_prev_call        },
	{'L',       0, "Resume previous call",hold_prev_call        },
	{'A', CMD_IPRM,"Switch audio device", switch_audio_dev      },

#ifdef USE_VIDEO
	{'E',       0, "Cycle video encoder", call_videoenc_cycle   },
	{'v',       0, "Video stream",        call_video_debug      },
#endif

	{'#',       0, NULL,                  digit_handler         },
	{'*',       0, NULL,                  digit_handler         },
	{'0',       0, NULL,                  digit_handler         },
	{'1',       0, NULL,                  digit_handler         },
	{'2',       0, NULL,                  digit_handler         },
	{'3',       0, NULL,                  digit_handler         },
	{'4',       0, NULL,                  digit_handler         },
	{'5',       0, NULL,                  digit_handler         },
	{'6',       0, NULL,                  digit_handler         },
	{'7',       0, NULL,                  digit_handler         },
	{'8',       0, NULL,                  digit_handler         },
	{'9',       0, NULL,                  digit_handler         },
	{0x00,      0, NULL,                  digit_handler         },

	{'S',       0, "Statusmode toggle",   toggle_statmode       },
};


static void menu_set_incall(bool incall)
{
	/* Dynamic menus */
	if (incall) {
		(void)cmd_register(callcmdv, ARRAY_SIZE(callcmdv));
	}
	else {
		cmd_unregister(callcmdv);
	}
}


static void tmrstat_handler(void *arg)
{
	struct call *call;
	(void)arg;

	/* the UI will only show the current active call */
	call = ua_call(uag_cur());
	if (!call)
		return;

	tmr_start(&tmr_stat, 100, tmrstat_handler, 0);

	if (ui_isediting())
		return;

	if (STATMODE_OFF != statmode) {
		(void)re_fprintf(stderr, "%H\r", call_status, call);
	}
}


static void update_callstatus(void)
{
	/* if there are any active calls, enable the call status view */
	if (have_active_calls())
		tmr_start(&tmr_stat, 100, tmrstat_handler, 0);
	else
		tmr_cancel(&tmr_stat);
}


static void alert_start(void *arg)
{
	(void)arg;

	if (!menu.bell)
		return;

	ui_output("\033[10;1000]\033[11;1000]\a");

	tmr_start(&tmr_alert, 1000, alert_start, NULL);
}


static void alert_stop(void)
{
	if (!menu.bell)
		return;

	if (tmr_isrunning(&tmr_alert))
		ui_output("\r");

	tmr_cancel(&tmr_alert);
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	(void)call;
	(void)prm;
	(void)arg;

	switch (ev) {

	case UA_EVENT_CALL_INCOMING:

		/* set the current User-Agent to the one with the call */
		uag_current_set(ua);

		info("%s: Incoming call from: %s %s -"
		     " (press ENTER to accept)\n",
		     ua_aor(ua), call_peername(call), call_peeruri(call));

		/* stop any ringtones */
		menu.play = mem_deref(menu.play);

		/* Only play the ringtones if answermode is "Manual".
		 * If the answermode is "auto" then be silent.
		 */
		if (ANSWERMODE_MANUAL == account_answermode(ua_account(ua))) {

			if (list_count(ua_calls(ua)) > 1) {
				(void)play_file(&menu.play,
						"callwaiting.wav", 3);
			}
			else {
				/* Alert user */
				(void)play_file(&menu.play, "ring.wav", -1);
			}

			if (menu.bell)
				alert_start(0);
		}
		break;

	case UA_EVENT_CALL_RINGING:
		/* stop any ringtones */
		menu.play = mem_deref(menu.play);

		(void)play_file(&menu.play, "ringback.wav", -1);
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		/* stop any ringtones */
		menu.play = mem_deref(menu.play);

		alert_stop();
		break;

	case UA_EVENT_CALL_CLOSED:
		/* stop any ringtones */
		menu.play = mem_deref(menu.play);

		if (call_scode(call)) {
			const char *tone;
			tone = translate_errorcode(call_scode(call));
			if (tone)
				(void)play_file(&menu.play, tone, 1);
		}

		alert_stop();
		break;

	case UA_EVENT_REGISTER_OK:
		check_registrations();
		break;

	case UA_EVENT_UNREGISTERING:
		return;

	default:
		break;
	}

	menu_set_incall(have_active_calls());
	update_callstatus();
}


static void message_handler(const struct pl *peer, const struct pl *ctype,
			    struct mbuf *body, void *arg)
{
	(void)ctype;
	(void)arg;

	(void)re_fprintf(stderr, "\r%r: \"%b\"\n", peer,
			 mbuf_buf(body), mbuf_get_left(body));

	(void)play_file(NULL, "message.wav", 0);
}


static int module_init(void)
{
	int err;

	conf_get_bool(conf_cur(), "menu_bell", &menu.bell);

	dialbuf = mbuf_alloc(64);
	if (!dialbuf)
		return ENOMEM;

	start_ticks = tmr_jiffies();
	(void)time(&start_time);
	tmr_init(&tmr_alert);
	statmode = STATMODE_CALL;

	err  = cmd_register(cmdv, ARRAY_SIZE(cmdv));
	err |= uag_event_register(ua_event_handler, NULL);

	err |= message_init(message_handler, NULL);

	return err;
}


static int module_close(void)
{
	message_close();
	uag_event_unregister(ua_event_handler);
	cmd_unregister(cmdv);

	menu_set_incall(false);
	tmr_cancel(&tmr_alert);
	tmr_cancel(&tmr_stat);
	dialbuf = mem_deref(dialbuf);

	le_cur = NULL;

	menu.play = mem_deref(menu.play);

	return 0;
}


const struct mod_export DECL_EXPORTS(menu) = {
	"menu",
	"application",
	module_init,
	module_close
};
