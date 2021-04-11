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

	if (carg->prm) {
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
	uint32_t linenum = atoi(carg->prm);

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

	if (carg->prm) {
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


static int call_video_debug(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();

	return video_debug(pf, call_video(ua_call(ua)));
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

{"aubitrate",    0,  CMD_PRM, "Set audio bitrate",    set_audio_bitrate    },
{"audio_debug", 'A',       0, "Audio stream",         call_audio_debug     },
{"callfind",     0,  CMD_PRM, "Find call <callid>",   cmd_find_call        },
{"hold",        'x',       0, "Call hold",            cmd_call_hold        },
{"line",        '@', CMD_PRM, "Set current call <line>", set_current_call  },
{"mute",        'm',       0, "Call mute/un-mute",    call_mute            },
{"reinvite",    'I',       0, "Send re-INVITE",       call_reinvite        },
{"resume",      'X',       0, "Call resume",          cmd_call_resume      },
{"sndcode",      0,  CMD_PRM, "Send Code",            send_code            },
{"statmode",    'S',       0, "Statusmode toggle",    toggle_statmode      },
{"transfer",    't', CMD_PRM, "Transfer call",        call_xfer            },
{"video_debug", 'V',       0, "Video stream",         call_video_debug     },
{"videodir",      0, CMD_PRM, "Set video direction",  set_video_dir        },

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
