/**
 * @file dynamic_menu.c  dynamic menu related functions
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <stdlib.h>
#include <re.h>
#include <baresip.h>
#include "menu.h"


static int set_audio_bitrate(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call;
	uint32_t bitrate = str_isset(carg->prm) ? atoi(carg->prm) : 0;

	call = ua_call(ua);
	if (call) {
		(void)re_hprintf(pf, "setting audio bitrate: %u bps\n",
				 bitrate);
		audio_set_bitrate(call_audio(call), bitrate);
	}
	else {
		(void)re_hprintf(pf, "call not found\n");
		return EINVAL;
	}

	return 0;
}


static int call_audio_debug(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();

	return audio_debug(pf, call_audio(ua_call(ua)));
}


static int cmd_find_call(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	const char *id = carg->prm;
	struct call *call = uag_call_find(id);

	if (call) {
		(void)re_hprintf(pf, "setting current call: %s\n", id);
		menu_selcall(call);
	}
	else {
		(void)re_hprintf(pf, "call not found (id=%s)\n", id);
		return EINVAL;
	}

	return 0;
}


/**
 * Put the active call on-hold
 *
 * @param pf   Print handler
 * @param arg  Command arguments (carg)
 *             carg->data is an optional pointer to a User-Agent
 *             carg->prm is an optional call-id string
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_call_hold(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call = ua_call(ua);

	(void)pf;

	if (str_isset(carg->prm)) {
		call = uag_call_find(carg->prm);
		if (!call) {
			re_hprintf(pf, "call %s not found\n", carg->prm);
			return EINVAL;
		}
	}

	if (!call) {
		re_hprintf(pf, "no active call\n");
		return ENOENT;
	}

	return call_hold(call, true);
}


static int set_current_call(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call;
	uint32_t linenum = 0;

	if (str_isset(carg->prm)) {
		linenum = atoi(carg->prm);
	}

	call = call_find_linenum(ua_calls(ua), linenum);
	if (call) {
		(void)re_hprintf(pf, "setting current call: line %u\n",
				 linenum);
		menu_selcall(call);
	}
	else {
		(void)re_hprintf(pf, "call not found (ua=%s, line=%u)\n",
				account_aor(ua_account(ua)), linenum);
		return EINVAL;
	}

	return 0;
}


/**
 * Mute the active call
 *
 * @param pf   Print handler
 * @param arg  Command arguments (carg)
 *             carg->data is an optional pointer to a User-Agent
 *             carg->prm is an optional string (yes,no,true,false,on,off,...)
 *
 * @return 0 if success, otherwise errorcode
 */
static int call_mute(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct audio *audio = call_audio(ua_call(ua));
	bool muted = !audio_ismuted(audio);

	if (str_isset(carg->prm)) {
		int err = str_bool(&muted, carg->prm);
		if (err) {
			(void)re_hprintf(pf, "invalid mute value: %s.\n",
					 carg->prm);
			return err;
		}
	}

	(void)re_hprintf(pf, "\ncall %smuted\n", muted ? "" : "un-");
	audio_mute(audio, muted);

	return 0;
}


static int call_reinvite(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	(void)pf;

	return call_modify(ua_call(ua));
}


/**
 * Resume the active call
 *
 * @param pf   Print handler
 * @param arg  Command arguments (carg)
 *             carg->data is an optional pointer to a User-Agent
 *             carg->prm is an optional call-id string
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_call_resume(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call = ua_call(ua);
	(void)pf;

	if (str_isset(carg->prm)) {
		call = uag_call_find(carg->prm);
		if (!call) {
			re_hprintf(pf, "call %s not found\n", carg->prm);
			return EINVAL;
		}
	}

	if (!call) {
		re_hprintf(pf, "no active call\n");
		return ENOENT;
	}

	return uag_hold_resume(call);
}


static int send_code(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call;
	size_t i;
	int err = 0;
	(void)pf;

	call = ua_call(ua);
	if (call) {
		for (i = 0; i < str_len(carg->prm) && !err; i++) {
			err = call_send_digit(call, carg->prm[i]);
		}
		if (!err) {
			err = call_send_digit(call, KEYCODE_REL);
		}
	}

	return err;
}


static int toggle_statmode(struct re_printf *pf, void *arg)
{
	struct menu *menu = menu_get();

	(void)pf;
	(void)arg;

	if (menu->statmode == STATMODE_OFF)
		menu->statmode = STATMODE_CALL;
	else
		menu->statmode = STATMODE_OFF;

	return 0;
}


static int call_xfer(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	int err = 0;
	(void)pf;

	err = call_hold(ua_call(ua), true);
	if (err)
		return err;

	return call_transfer(ua_call(ua), carg->prm);
}


static int attended_xfer(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct menu *menu = menu_get();
	int err = 0;

	(void)pf;

	if (!str_len(carg->prm)) {
		info ("menu: no transfer target specified\n");
		goto out;
	}

	menu->xfer_call = ua_call(ua);

	if (!call_supported(menu->xfer_call, REPLACES)) {
		info ("menu: peer does not support Replaces header\n");
		goto out;
	}

	err = call_hold(ua_call(ua), true);
	if (err)
		goto out;

	err = ua_connect(ua, &menu->xfer_targ, NULL, carg->prm, VIDMODE_ON);

	if (err)
		goto out;

	call_set_user_data(menu->xfer_targ, call_user_data(menu->xfer_call));
 out:
	return err;

}


static int exec_att_xfer(struct re_printf *pf, void *arg)
{
	struct menu *menu = menu_get();
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	int err = 0;

	(void) pf;

	if (menu->xfer_call) {
		err = call_hold(ua_call(ua), true);
		if (err)
			goto out;

		err = call_replace_transfer(menu->xfer_call, ua_call(ua));
	}
	else {
		info ("menu: no pending attended call transfer available\n");
		err = ECANCELED;
	}

 out:
	menu->xfer_call = NULL;
	menu->xfer_targ = NULL;

	return err;
}


static int abort_att_xfer(struct re_printf *pf, void *arg)
{
	struct menu *menu = menu_get();

	(void) pf;
	(void) arg;

	menu->xfer_call = NULL;
	menu->xfer_targ = NULL;

	return 0;
}


static int call_video_debug(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();

	return video_debug(pf, call_video(ua_call(ua)));
}


static int set_media_ldir(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct call *call = menu_callcur();
	struct pl argdir[2] = {PL_INIT, PL_INIT};
	enum sdp_dir adir, vdir;
	struct pl callid = PL_INIT;
	char *cid = NULL;
	bool ok = false;

	const char *usage = "usage: /medialdir"
			" audio=<inactive, sendonly, recvonly, sendrecv>"
			" video=<inactive, sendonly, recvonly, sendrecv>"
			" [callid=id]\n"
			"/medialdir <sendonly, recvonly, sendrecv> [id]\n"
			"Audio & video must not be"
			" inactive at the same time\n";

	ok |= 0 == menu_param_decode(carg->prm, "audio", &argdir[0]);
	ok |= 0 == menu_param_decode(carg->prm, "video", &argdir[1]);
	ok |= 0 == menu_param_decode(carg->prm, "callid", &callid);
	if (!ok) {
		ok = 0 == re_regex(carg->prm, str_len(carg->prm),
			"[^ ]*[ \t\r\n]*[^ ]*", &argdir[0], NULL, &callid);
	}

	if (!ok) {
		(void) re_hprintf(pf, "%s", usage);
		return EINVAL;
	}

	if (!pl_isset(&argdir[1]))
		argdir[1] = argdir[0];

	adir = sdp_dir_decode(&argdir[0]);
	vdir = sdp_dir_decode(&argdir[1]);
	if (adir == SDP_INACTIVE && vdir == SDP_INACTIVE) {
		(void) re_hprintf(pf, "%s", usage);
		return EINVAL;
	}

	(void)pl_strdup(&cid, &callid);
	if (str_isset(cid))
		call = uag_call_find(cid);

	cid = mem_deref(cid);
	if (!call)
		return EINVAL;

	call_set_media_direction(call, adir, vdir);
	return 0;
}


static int stop_ringing(struct re_printf *pf, void *arg)
{
	struct menu *menu;
	struct play *p;

	(void)pf;
	(void)arg;

	menu = menu_get();
	p = menu->play;
	menu->play = NULL;

	if (p) {
		mem_deref(p);
	}

	return 0;
}


static int set_video_dir(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct call *call = menu_callcur();
	int err = 0;

	if (!call)
		return EINVAL;

	if (!call_refresh_allowed(call)) {
		(void)re_hprintf(pf, "video update not allowed currently");
		return EINVAL;
	}

	if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_INACTIVE))) {
		err = call_set_video_dir(call, SDP_INACTIVE);
	}
	else if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_SENDONLY))) {
		err = call_set_video_dir(call, SDP_SENDONLY);
	}
	else if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_RECVONLY))) {
		err = call_set_video_dir(call, SDP_RECVONLY);
	}
	else if (0 == str_cmp(carg->prm, sdp_dir_name(SDP_SENDRECV))) {
		err = call_set_video_dir(call, SDP_SENDRECV);
	}
	else {
		(void)re_hprintf(pf, "invalid video direction %s"
			" (inactive, sendonly, recvonly, sendrecv)\n",
			carg->prm);
		return EINVAL;
	}

	return err;
}


static int digit_handler(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	(void) pf;

	return call_send_digit(menu_callcur(), carg->key);
}


/*Dynamic call menu*/
static const struct cmd callcmdv[] = {

{"aubitrate",    0,  CMD_PRM, "Set audio bitrate",    set_audio_bitrate    },
{"audio_debug", 'A',       0, "Audio stream",         call_audio_debug     },
{"callfind",     0,  CMD_PRM, "Find call <callid>",   cmd_find_call        },
{"hold",        'x',       0, "Call hold",            cmd_call_hold        },
{"line",        '@', CMD_PRM, "Set current call <line>", set_current_call  },
{"mute",        'm', CMD_PRM, "Call mute/un-mute",    call_mute            },
{"reinvite",    'I',       0, "Send re-INVITE",       call_reinvite        },
{"resume",      'X',       0, "Call resume",          cmd_call_resume      },
{"sndcode",      0,  CMD_PRM, "Send Code",            send_code            },
{"statmode",    'S',       0, "Statusmode toggle",    toggle_statmode      },
{"transfer",    't', CMD_PRM, "Transfer call",        call_xfer            },
{"atransferstart", 'T', CMD_PRM, "Start attended transfer", attended_xfer  },
{"atransferexec",   0,    0, "Execute attended transfer",   exec_att_xfer  },
{"atransferabort",  0,    0, "Abort attended transfer",     abort_att_xfer },
{"video_debug", 'V',       0, "Video stream",         call_video_debug     },
{"videodir",      0, CMD_PRM, "Set video direction",  set_video_dir        },
{"medialdir",     0, CMD_PRM, "Set local media direction",  set_media_ldir },
{"stopringing",   0,       0, "Stop ring tones",      stop_ringing         },

/* Numeric keypad for DTMF events: */
{NULL, '#',         0, NULL,                  digit_handler         },
{NULL, '*',         0, NULL,                  digit_handler         },
{NULL, '0',         0, NULL,                  digit_handler         },
{NULL, '1',         0, NULL,                  digit_handler         },
{NULL, '2',         0, NULL,                  digit_handler         },
{NULL, '3',         0, NULL,                  digit_handler         },
{NULL, '4',         0, NULL,                  digit_handler         },
{NULL, '5',         0, NULL,                  digit_handler         },
{NULL, '6',         0, NULL,                  digit_handler         },
{NULL, '7',         0, NULL,                  digit_handler         },
{NULL, '8',         0, NULL,                  digit_handler         },
{NULL, '9',         0, NULL,                  digit_handler         },
{NULL, KEYCODE_REL, 0, NULL,                  digit_handler         },

};


/**
 * Register call commands
 *
 * @return int 0 if success, errorcode otherwise
 */
int dynamic_menu_register(void)
{
	struct commands *baresip_cmd = baresip_commands();

	if (!cmds_find(baresip_cmd, callcmdv))
		return cmd_register(baresip_cmd,
			callcmdv, RE_ARRAY_SIZE(callcmdv));

	return 0;
}


/**
 * Unregister call commands
 */
void dynamic_menu_unregister(void)
{
	cmd_unregister(baresip_commands(), callcmdv);
}
