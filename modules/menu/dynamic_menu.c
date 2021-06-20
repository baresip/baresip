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


static int call_mute(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct audio *audio = call_audio(ua_call(ua));
	bool muted = !audio_ismuted(audio);

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
	(void)pf;

	return call_transfer(ua_call(ua), carg->prm);
}


static int call_att_xfer(struct re_printf *pf, void *arg)
{
	struct menu *menu = menu_get();
	struct call *call = uag_call_find(menu->callid);

	if (menu->attended_callid) {
		re_hprintf(pf, "attended transfer already in process\n");
		return ENOENT;
	}
	else {
		if (!call_is_onhold(call)) {
			re_hprintf(pf, "please hold your active call\n");
			return ENOENT;
		}
		str_dup(&menu->attended_callid, menu->callid);
		dial_handler(pf, arg);
	}

	return 0;
}


static int transfer_att_xfer(struct re_printf *pf, void *unused)
{
	struct menu *menu = menu_get();
	struct call *call = uag_call_find(menu->callid);
	struct call *attended_call;
	(void)unused;

	if (menu->attended_callid) {
		attended_call = uag_call_find(menu->attended_callid);
		if (!call_is_onhold(call)) {
			re_hprintf(pf, "please hold your active call\n");
			return ENOENT;
		}
		mem_deref(menu->attended_callid);
		menu->attended_callid = NULL;
		call_replace_transfer(attended_call, call);
	}
	else {
		re_hprintf(pf, "no attended to transfer call\n");
		return ENOENT;
	}

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

	adir = decode_sdp_enum(&argdir[0]);
	vdir = decode_sdp_enum(&argdir[1]);
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

	return call_set_media_direction(call, adir, vdir);
}


static int set_video_dir(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct call *call = menu_callcur();
	int err = 0;

	if (!call)
		return EINVAL;

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

{"aubitrate",      0, CMD_PRM, "Set audio bitrate",        set_audio_bitrate },
{"audio_debug",  'A',       0, "Audio stream",             call_audio_debug  },
{"callfind",       0, CMD_PRM, "Find call <callid>",       cmd_find_call     },
{"hold",         'x',       0, "Call hold",                cmd_call_hold     },
{"line",         '@', CMD_PRM, "Set current call <line>",  set_current_call  },
{"mute",         'm',       0, "Call mute/un-mute",        call_mute         },
{"reinvite",     'I',       0, "Send re-INVITE",           call_reinvite     },
{"resume",       'X',       0, "Call resume",              cmd_call_resume   },
{"sndcode",        0, CMD_PRM, "Send Code",                send_code         },
{"statmode",     'S',       0, "Statusmode toggle",        toggle_statmode   },
{"transfer",     't', CMD_PRM, "Transfer call",            call_xfer         },
{"att_transfer", 'T', CMD_PRM, "Attended call transfer",   call_att_xfer     },
{"forw_transfer",'F',       0, "Forward attended call",    transfer_att_xfer },
{"video_debug",  'V',       0, "Video stream",             call_video_debug  },
{"videodir",       0, CMD_PRM, "Set video direction",      set_video_dir     },
{"medialdir",      0, CMD_PRM, "Set local media direction", set_media_ldir   },

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
			callcmdv, ARRAY_SIZE(callcmdv));

	return 0;
}


/**
 * Unregister call commands
 */
void dynamic_menu_unregister(void)
{
	cmd_unregister(baresip_commands(), callcmdv);
}
