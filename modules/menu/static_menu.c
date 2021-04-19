/**
 * @file static_menu.c Static menu related functions
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <stdlib.h>
#include <re.h>
#include <baresip.h>

#include "menu.h"


/**
 * Decode a SDP direction
 *
 * @param pl  SDP direction as string
 *
 * @return sdp_dir SDP direction, SDP_SENDRECV as fallback
 */
static enum sdp_dir decode_sdp_enum(const struct pl *pl)
{
	if (!pl_strcmp(pl, "inactive")) {
		return SDP_INACTIVE;
	}
	else if (!pl_strcmp(pl, "sendonly")) {
		return  SDP_SENDONLY;
	}
	else if (!pl_strcmp(pl, "recvonly")) {
		return SDP_RECVONLY;
	}

	return SDP_SENDRECV;
}


static const char about_fmt[] =
	".------------------------------------------------------------.\n"
	"|                      "
	"\x1b[34;1m" "bare"
	"\x1b[31;1m" "sip"
	"\x1b[;m"
	" %-10s                    |\n"
	"|                                                            |\n"
	"| Baresip is a portable and modular SIP User-Agent           |\n"
	"| with audio and video support                               |\n"
	"|                                                            |\n"
	"| License:   BSD                                             |\n"
	"| Homepage:  https://github.com/baresip/baresip              |\n"
	"|                                                            |\n"
	"'------------------------------------------------------------'\n"
	;


static int about_box(struct re_printf *pf, void *unused)
{
	(void)unused;

	return re_hprintf(pf, about_fmt, BARESIP_VERSION);
}


static int answer_call(struct ua *ua, struct call *call)
{
	struct menu *menu = menu_get();
	int err;

	if (!call)
		return EINVAL;

	/* Stop any ongoing ring-tones */
	menu->play = mem_deref(menu->play);

	err  = uag_hold_others(call);
	err |= ua_answer(ua, call, VIDMODE_ON);
	return err;
}


/**
 * Answers active incoming call
 *
 * @param pf   Print handler
 * @param arg  Command arguments (carg)
 *             carg->data is an optional pointer to a User-Agent
 *             carg->prm is an optional call-id string
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_answer(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call = ua_call(ua);
	int err;

	if (carg->prm) {
		call = uag_call_find(carg->prm);
		if (!call) {
			re_hprintf(pf, "call %s not found\n", carg->prm);
			return EINVAL;
		}

		ua = call_get_ua(call);
	}
	else if (call_state(call) != CALL_STATE_INCOMING) {
		call = menu_find_call_state(CALL_STATE_INCOMING);
		ua = call_get_ua(call);
	}

	err = answer_call(ua, call);
	if (err)
		re_hprintf(pf, "could not answer call (%m)\n", err);

	return err;
}


/**
 * Accepts the pending call with special audio and video direction
 *
 * @param pf     Print handler for debug output
 * @param arg    Command arguments
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_answerdir(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	enum sdp_dir adir, vdir;
	struct pl argdir[2] = {PL_INIT, PL_INIT};
	struct pl callid = PL_INIT;
	char *cid = NULL;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call;
	int err = 0;
	bool ok = false;

	const char *usage = "usage: /acceptdir"
			" audio=<inactive, sendonly, recvonly, sendrecv>"
			" video=<inactive, sendonly, recvonly, sendrecv>"
			" [callid=id]\n"
			"/acceptdir <sendonly, recvonly, sendrecv> [id]\n"
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

	call = ua_call(ua);

	(void)pl_strdup(&cid, &callid);
	if (str_isset(cid)) {
		call = uag_call_find(cid);
		cid = mem_deref(cid);
		ua = call_get_ua(call);
	}
	else if (call_state(call) != CALL_STATE_INCOMING) {
		call = menu_find_call_state(CALL_STATE_INCOMING);
		ua = call_get_ua(call);
	}

	(void)call_set_media_direction(call, adir, vdir);
	err = answer_call(ua, call);
	if (err)
		re_hprintf(pf, "could not answer call (%m)\n", err);

	return err;
}


static int cmd_set_answermode(struct re_printf *pf, void *arg)
{
	enum answermode mode;
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data;
	struct le *le;
	int err;

	if (0 == str_cmp(carg->prm, "manual")) {
		mode = ANSWERMODE_MANUAL;
	}
	else if (0 == str_cmp(carg->prm, "early")) {
		mode = ANSWERMODE_EARLY;
	}
	else if (0 == str_cmp(carg->prm, "auto")) {
		mode = ANSWERMODE_AUTO;
	}
	else {
		(void)re_hprintf(pf, "Invalid answer mode: %s\n", carg->prm);
		return EINVAL;
	}

	if (ua) {
		err = account_set_answermode(ua_account(ua), mode);
		if (err)
			return err;
	}
	else {
		for (le = list_head(uag_list()); le; le = le->next) {
			ua = le->data;
			err = account_set_answermode(ua_account(ua), mode);
			if (err)
				return err;
		}
	}

	(void)re_hprintf(pf, "Answer mode changed to: %s\n", carg->prm);

	return 0;
}


static int switch_audio_player(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl_driver, pl_device;
	struct config_audio *aucfg;
	struct config *cfg;
	struct audio *a;
	const struct auplay *ap;
	struct le *le;
	struct le *leu;
	char driver[16], device[128] = "";
	int err = 0;

	if (re_regex(carg->prm, str_len(carg->prm), "[^,]+,[~]*",
		     &pl_driver, &pl_device)) {

		return re_hprintf(pf, "\rFormat should be:"
				  " driver,device\n");
	}

	pl_strcpy(&pl_driver, driver, sizeof(driver));
	pl_strcpy(&pl_device, device, sizeof(device));

	ap = auplay_find(baresip_auplayl(), driver);
	if (!ap) {
		re_hprintf(pf, "no such audio-player: %s\n", driver);
		return 0;
	}
	else if (!list_isempty(&ap->dev_list)) {

		if (!mediadev_find(&ap->dev_list, device)) {
			re_hprintf(pf,
				   "no such device for %s audio-player: %s\n",
				   driver, device);

			mediadev_print(pf, &ap->dev_list);

			return 0;
		}
	}

	re_hprintf(pf, "switch audio player: %s,%s\n",
		   driver, device);

	cfg = conf_config();
	if (!cfg) {
		return re_hprintf(pf, "no config object\n");
	}

	aucfg = &cfg->audio;

	str_ncpy(aucfg->play_mod, driver, sizeof(aucfg->play_mod));
	str_ncpy(aucfg->play_dev, device, sizeof(aucfg->play_dev));

	str_ncpy(aucfg->alert_mod, driver, sizeof(aucfg->alert_mod));
	str_ncpy(aucfg->alert_dev, device, sizeof(aucfg->alert_dev));

	for (leu = list_head(uag_list()); leu; leu = leu->next) {
		struct ua *ua = leu->data;
		for (le = list_tail(ua_calls(ua)); le; le = le->prev) {

			struct call *call = le->data;

			a = call_audio(call);

			err = audio_set_player(a, driver, device);
			if (err) {
				re_hprintf(pf, "failed to set audio-player"
						" (%m)\n", err);
				break;
			}
		}
	}

	return 0;
}


static int switch_audio_source(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl_driver, pl_device;
	struct config_audio *aucfg;
	struct config *cfg;
	struct audio *a;
	const struct ausrc *as;
	struct le *le;
	struct le *leu;
	char driver[16], device[128] = "";
	int err = 0;

	if (re_regex(carg->prm, str_len(carg->prm), "[^,]+,[~]*",
		     &pl_driver, &pl_device)) {

		return re_hprintf(pf, "\rFormat should be:"
				  " driver,device\n");
	}

	pl_strcpy(&pl_driver, driver, sizeof(driver));
	pl_strcpy(&pl_device, device, sizeof(device));

	as = ausrc_find(baresip_ausrcl(), driver);
	if (!as) {
		re_hprintf(pf, "no such audio-source: %s\n", driver);
		return 0;
	}
	else if (!list_isempty(&as->dev_list)) {

		if (!mediadev_find(&as->dev_list, device)) {
			re_hprintf(pf,
				   "no such device for %s audio-source: %s\n",
				   driver, device);

			mediadev_print(pf, &as->dev_list);

			return 0;
		}
	}

	re_hprintf(pf, "switch audio device: %s,%s\n",
		   driver, device);

	cfg = conf_config();
	if (!cfg) {
		return re_hprintf(pf, "no config object\n");
	}

	aucfg = &cfg->audio;

	str_ncpy(aucfg->src_mod, driver, sizeof(aucfg->src_mod));
	str_ncpy(aucfg->src_dev, device, sizeof(aucfg->src_dev));

	for (leu = list_head(uag_list()); leu; leu = leu->next) {
		struct ua *ua = leu->data;
		for (le = list_tail(ua_calls(ua)); le; le = le->prev) {

			struct call *call = le->data;

			a = call_audio(call);

			err = audio_set_source(a, driver, device);
			if (err) {
				re_hprintf(pf, "failed to set audio-source"
						" (%m)\n", err);
				break;
			}
		}
	}

	return 0;
}


/**
 * Print the current SIP Call status for the current User-Agent
 *
 * @param pf     Print handler for debug output
 * @param unused Unused parameter
 *
 * @return 0 if success, otherwise errorcode
 */
static int ua_print_call_status(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call;
	int err;

	call = ua_call(ua);
	if (call) {
		err  = re_hprintf(pf, "\n%H\n", call_debug, call);
	}
	else {
		err  = re_hprintf(pf, "\n(no active calls)\n");
	}

	return err;
}


static void clean_number(char *str)
{
	int i = 0, k = 0;

	/* only clean numeric numbers
	 * In other cases trust the user input
	 */
	int err = re_regex(str, str_len(str), "[A-Za-z]");
	if (err == 0)
		return;

	/* remove (0) which is in some mal-formated numbers
	 * but only if trailed by another character
	 */
	if (str[0] == '+' || (str[0] == '0' && str[1] == '0'))
		while (str[i]) {
			if (str[i] == '('
			 && str[i+1] == '0'
			 && str[i+2] == ')'
			 && (str[i+3] == ' '
				 || (str[i+3] >= '0' && str[i+3] <= '9')
			    )
			) {
				str[i+1] = ' ';
				break;
			}
			++i;
		}
	i = 0;
	while (str[i]) {
		if (str[i] == ' '
		 || str[i] == '.'
		 || str[i] == '-'
		 || str[i] == '/'
		 || str[i] == '('
		 || str[i] == ')')
			++i;
		else
			str[k++] = str[i++];
	}
	str[k] = '\0';
}


static enum answer_method auto_answer_method(struct re_printf *pf)
{
	struct pl met;
	int err;

	err = conf_get(conf_cur(), "sip_autoanswer_method", &met);
	if (err)
		return ANSM_NONE;

	if (!pl_strcmp(&met, "rfc5373")) {
		return ANSM_RFC5373;
	}
	else if (!pl_strcmp(&met, "call-info")) {
		return ANSM_CALLINFO;
	}
	else if (!pl_strcmp(&met, "alert-info")) {
		return ANSM_ALERTINFO;
	}
	else {
		(void)re_hprintf(pf, "SIP auto answer method %r is not"
				 " supported", met);
		return ANSM_NONE;
	}
}


static int dial_handler(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct menu *menu = menu_get();
	struct pl word[2] = {PL_INIT, PL_INIT};
	struct ua *ua = menu_ua_carg(pf, carg, &word[0], &word[1]);
	char *uri = NULL;
	struct mbuf *uribuf = NULL;
	struct call *call;
	int err = 0;

	(void)pf;

	if (pl_isset(&word[0])) {
		err = pl_strdup(&uri, &word[0]);
		if (err)
			return err;
	}

	if (str_isset(uri)) {

		mbuf_rewind(menu->dialbuf);
		(void)mbuf_write_str(menu->dialbuf, uri);
		if (menu->clean_number)
			clean_number(uri);

	}
	else if (menu->dialbuf->end > 0) {

		menu->dialbuf->pos = 0;
		err = mbuf_strdup(menu->dialbuf, &uri, menu->dialbuf->end);
		if (err)
			goto out;

		if (menu->clean_number)
			clean_number(uri);
	}

	if (!ua)
		ua = uag_find_requri(uri);

	if (!ua) {
		re_hprintf(pf, "could not find UA for %s\n", uri);
		err = EINVAL;
		goto out;
	}

	uribuf = mbuf_alloc(64);
	if (!uribuf)
		return ENOMEM;

	err = account_uri_complete(ua_account(ua), uribuf, uri);
	if (err) {
		(void)re_hprintf(pf, "ua_connect failed to complete uri\n");
		return EINVAL;
	}

	mem_deref(uri);

	uribuf->pos = 0;
	err = mbuf_strdup(uribuf, &uri, uribuf->end);
	if (err)
		goto out;

	if (menu->adelay >= 0)
		(void)ua_enable_autoanswer(ua, menu->adelay,
				auto_answer_method(pf));


	err = ua_connect(ua, &call, NULL, uri, VIDMODE_ON);

	if (menu->adelay >= 0)
		(void)ua_disable_autoanswer(ua, auto_answer_method(pf));
	if (err) {
		(void)re_hprintf(pf, "ua_connect failed: %m\n", err);
		goto out;
	}

	re_hprintf(pf, "call id: %s\n", call_id(call));

out:
	mem_deref(uribuf);
	mem_deref(uri);
	return err;
}


static int cmd_dialdir(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	enum sdp_dir adir, vdir;
	struct pl argdir[2] = {PL_INIT, PL_INIT};
	struct pl pluri;
	struct call *call;
	char *uri = NULL;
	struct mbuf *uribuf = NULL;
	struct ua *ua = carg->data;
	int err = 0;

	const char *usage = "usage: /dialdir <address/number>"
			" audio=<inactive, sendonly, recvonly, sendrecv>"
			" video=<inactive, sendonly, recvonly, sendrecv>\n"
			"/dialdir <address/number>"
			" <sendonly, recvonly, sendrecv>\n"
			"Audio & video must not be"
			" inactive at the same time\n";

	err = re_regex(carg->prm, str_len(carg->prm),
		"[^ ]* audio=[^ ]* video=[^ ]*",
		&pluri, &argdir[0], &argdir[1]);
	if (err)
		err = re_regex(carg->prm, str_len(carg->prm),
			"[^ ]* [^ ]*",&pluri, &argdir[0]);

	if (err) {
		(void)re_hprintf(pf, "%s", usage);
		return EINVAL;
	}

	if (!pl_isset(&argdir[1]))
		argdir[1] = argdir[0];

	adir = decode_sdp_enum(&argdir[0]);
	vdir = decode_sdp_enum(&argdir[1]);

	if (adir == SDP_INACTIVE && vdir == SDP_INACTIVE) {
		(void)re_hprintf(pf, "%s", usage);
		return EINVAL;
	}

	err = pl_strdup(&uri, &pluri);
	if (err)
		goto out;

	if (!ua)
		ua = uag_find_requri(carg->prm);

	if (!ua) {
		(void)re_hprintf(pf, "could not find UA for %s\n", carg->prm);
		err = EINVAL;
		goto out;
	}

	uribuf = mbuf_alloc(64);
	if (!uribuf)
		return ENOMEM;

	err = account_uri_complete(ua_account(ua), uribuf, uri);
	if (err) {
		(void)re_hprintf(pf, "ua_connect failed to complete uri\n");
		return EINVAL;
	}

	mem_deref(uri);

	uribuf->pos = 0;
	err = mbuf_strdup(uribuf, &uri, uribuf->end);
	if (err)
		goto out;

	err = ua_connect_dir(ua, &call, NULL, uri, VIDMODE_ON, adir, vdir);
	if (err)
		goto out;

	re_hprintf(pf, "call id: %s\n", call_id(call));

 out:
	mem_deref(uribuf);
	mem_deref(uri);

	return err;
}


static int cmd_dnd(struct re_printf *pf, void *arg)
{
	int err = 0;
	const struct cmd_arg *carg = arg;

	if (!str_isset(carg->prm)) {
		err = EINVAL;
		goto out;
	}

	if (!str_cmp(carg->prm, "true"))
		uag_set_dnd(true);
	else if (!str_cmp(carg->prm, "false"))
		uag_set_dnd(false);
	else
		err = EINVAL;

 out:
	if (err)
		re_hprintf(pf, "usage: /dnd <true|false>\n");

	return err;
}


/**
 * Hangup the active call
 *
 * @param pf   Print handler
 * @param arg  Command arguments (carg)
 *             carg->data is an optional pointer to a User-Agent
 *             carg->prm is an optional call-id string
 *
 * @return 0 if success, otherwise errorcode
 */

static int cmd_hangup(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = carg->data ? carg->data : menu_uacur();
	struct call *call = ua_call(ua);

	(void)pf;

	if (carg->prm) {
		call = uag_call_find(carg->prm);
		if (!call) {
			re_hprintf(pf, "call %s not found\n", carg->prm);
			return EINVAL;
		}

		ua = call_get_ua(call);
	}

	if (!ua) {
		re_hprintf(pf, "no active call\n");
		return ENOENT;
	}

	ua_hangup(ua, call, 0, NULL);

	return 0;
}


static void hangup_callstate(enum call_state state)
{
	struct ua *ua = NULL;
	struct call *call = NULL;
	struct le *lecall = NULL, *leua = NULL;
	struct list *calls = NULL;

	for (leua = list_head(uag_list()); leua; leua = leua->next) {
		ua = leua->data;
		calls = ua_calls(ua);
		lecall = list_head(calls);
		while (lecall) {
			call = lecall->data;
			lecall = lecall->next;

			if (call_state(call) == state ||
					state == CALL_STATE_UNKNOWN)
				ua_hangup(ua, call, 0, NULL);
		}
	}
}


/**
 * Hangup all calls with optional filter for outgoing or incoming
 *
 * @param pf   Print handler for debug output
 * @param arg  Command arguments (carg)
 *             carg->prm Can optionally set to "out", "in", "all".
 *             - out ... Hangup calls in state CALL_STATE_OUTGOING,
 *                       CALL_STATE_RINGING, CALL_STATE_EARLY
 *             - in  ... Hangup calls in state CALL_STATE_INCOMING
 *             - all ... Hangup all calls (default).
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_hangupall(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pldir;
	int err = 0;

	(void) pf;

	if (!str_len(carg->prm)) {
		pl_set_str(&pldir, "all");
	}
	else {
		err = re_regex(carg->prm, str_len(carg->prm),
			"dir=[^ ]*", &pldir);
		if (err)
			err = re_regex(carg->prm, str_len(carg->prm),
					"[^ ]*", &pldir);

		if (err)
			return err;
	}

	if (!pl_strcmp(&pldir, "all")) {
		hangup_callstate(CALL_STATE_UNKNOWN);
	}
	else if (!pl_strcmp(&pldir, "out")) {
		hangup_callstate(CALL_STATE_OUTGOING);
		hangup_callstate(CALL_STATE_RINGING);
		hangup_callstate(CALL_STATE_EARLY);
	}
	else if (!pl_strcmp(&pldir, "in")) {
		hangup_callstate(CALL_STATE_INCOMING);
	}
	else {
		err = EINVAL;
		goto out;
	}

  out:
	if (err)
		(void)re_hprintf(pf, "/hangupall dir=<all, in, out>\n");

	return err;
}


static int print_commands(struct re_printf *pf, void *unused)
{
	(void)unused;
	return cmd_print(pf, baresip_commands());
}


static int cmd_print_calls(struct re_printf *pf, void *arg)
{
	struct le *le;
	int err;
	(void) arg;

	for (le = list_head(uag_list()); le; le = le->next) {
		const struct ua *ua = le->data;
		err = ua_print_calls(pf, ua);
		if (err)
			return err;
	}

	return 0;
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
		     &msg->to.auri, mbuf_buf(msg->mb),
		     mbuf_get_left(msg->mb));
		return;
	}

	info("%r: OPTIONS failed: %u %r\n", &msg->to.auri,
	     msg->scode, &msg->reason);
}


static int options_command(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl word[2] = {PL_INIT, PL_INIT};
	struct ua *ua = menu_ua_carg(pf, carg, &word[0], &word[1]);
	char *uri = NULL;
	struct mbuf *uribuf = NULL;
	int err = 0;

	err = pl_strdup(&uri, &word[0]);
	if (err)
		goto out;

	if (!ua)
		ua = uag_find_requri(uri);

	if (!ua) {
		(void)re_hprintf(pf, "could not find UA for %s\n", uri);
		err = EINVAL;
		goto out;
	}

	uribuf = mbuf_alloc(64);
	if (!uribuf)
		return ENOMEM;

	err = account_uri_complete(ua_account(ua), uribuf, uri);
	if (err) {
		(void)re_hprintf(pf, "options_command failed to "
				 "complete uri\n");
		return EINVAL;
	}

	mem_deref(uri);

	uribuf->pos = 0;
	err = mbuf_strdup(uribuf, &uri, uribuf->end);
	if (err)
		goto out;

	err = ua_options_send(ua, uri, options_resp_handler, NULL);

out:
	mem_deref(uribuf);
	mem_deref(uri);
	if (err) {
		(void)re_hprintf(pf, "could not send options: %m\n", err);
	}

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
	uint32_t i = 0;

	(void)unused;

	err = re_hprintf(pf, "\n--- User Agents (%u) ---\n",
			 list_count(uag_list()));

	for (le = list_head(uag_list()); le && !err; le = le->next) {
		const struct ua *ua = le->data;

		err  = re_hprintf(pf, "%u - ", i++);
		err |= ua_print_status(pf, ua);
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Set SIP auto answer delay for outgoing calls
 *
 * @param pf     Print handler for debug output
 * @param arg    Optional command argument
 *		 An integer that specifies the answer delay in [seconds].
 *		 If no argument is specified, then SIP auto answer is disabled
 *		 for outgoing calls.
 *
 * @return 0 if success, otherwise errorcode
 */
static int cmd_set_adelay(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;

	if (!carg->prm) {
		menu_get()->adelay = -1;
		return 0;
	}

	menu_get()->adelay = atoi(carg->prm);
	if (menu_get()->adelay >= 0)
		(void)re_hprintf(pf, "SIP auto answer delay changed to %d\n",
				 menu_get()->adelay);
	else
		(void)re_hprintf(pf, "SIP auto answer delay disabled\n");

	return 0;
}


static int cmd_ua_delete(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = NULL;

	if (str_isset(carg->prm)) {
		ua = uag_find_aor(carg->prm);
	}

	if (!ua) {
		return ENOENT;
	}

	(void)re_hprintf(pf, "deleting ua: %s\n", carg->prm);
	mem_deref(ua);
	(void)ua_print_reg_status(pf, NULL);

	return 0;
}


static int cmd_ua_delete_all(struct re_printf *pf, void *unused)
{
	struct ua *ua = NULL;

	(void)unused;

	while (list_head(uag_list()))
	{
		ua = list_head(uag_list())->data;
		mem_deref(ua);
	}

	(void)ua_print_reg_status(pf, NULL);

	return 0;
}


static int cmd_ua_find(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = NULL;

	if (str_isset(carg->prm)) {
		ua = uag_find_aor(carg->prm);
	}

	if (!ua) {
		(void)re_hprintf(pf, "could not find User-Agent: %s\n",
				 carg->prm);
		return ENOENT;
	}

	(void)re_hprintf(pf, "ua: %s\n", account_aor(ua_account(ua)));

	menu_update_callstatus(uag_call_count());

	return 0;
}


static int create_ua(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua = NULL;
	struct account *acc;
	int err = 0;

	if (str_isset(carg->prm)) {

		(void)re_hprintf(pf, "Creating UA for %s ...\n", carg->prm);
		err = ua_alloc(&ua, carg->prm);
		if (err)
			goto out;
	}

	acc = ua_account(ua);
	if (account_regint(acc)) {
		if (!account_prio(acc))
			(void)ua_register(ua);
		else
			(void)ua_fallback(ua);
	}

	err = ua_print_reg_status(pf, NULL);

 out:
	if (err) {
		(void)re_hprintf(pf, "menu: create_ua failed: %m\n", err);
	}

	return err;
}


static int switch_video_source(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl_driver, pl_device;
	struct config_video *vidcfg;
	struct config *cfg;
	struct video *v;
	const struct vidsrc *vs;
	struct le *le;
	struct le *leu;
	char driver[16], device[128] = "";
	int err = 0;

	if (re_regex(carg->prm, str_len(carg->prm), "[^,]+,[~]*",
		     &pl_driver, &pl_device)) {

		(void)re_hprintf(pf, "usage: /vidsrc <driver>,<device>\n");
		return EINVAL;
	}

	pl_strcpy(&pl_driver, driver, sizeof(driver));
	pl_strcpy(&pl_device, device, sizeof(device));

	vs = vidsrc_find(baresip_vidsrcl(), driver);
	if (!vs) {
		(void)re_hprintf(pf, "no such video-source: %s\n", driver);
		return 0;
	}
	else if (!list_isempty(&vs->dev_list)) {

		if (!mediadev_find(&vs->dev_list, device)) {
			(void)re_hprintf(pf,
				   "no such device for %s video-source: %s\n",
				   driver, device);

			mediadev_print(pf, &vs->dev_list);

			return 0;
		}
	}

	(void)re_hprintf(pf, "switch video device: %s,%s\n", driver, device);

	cfg = conf_config();
	if (!cfg) {
		(void)re_hprintf(pf, "no config object\n");
		return EINVAL;
	}

	vidcfg = &cfg->video;

	str_ncpy(vidcfg->src_mod, driver, sizeof(vidcfg->src_mod));
	str_ncpy(vidcfg->src_dev, device, sizeof(vidcfg->src_dev));

	for (leu = list_head(uag_list()); leu; leu = leu->next) {
		struct ua *ua = leu->data;
		for (le = list_tail(ua_calls(ua)); le; le = le->prev) {

			struct call *call = le->data;

			v = call_video(call);

			err = video_set_source(v, driver, device);
			if (err) {
				(void)re_hprintf(pf,
						 "failed to set video-source"
						 " (%m)\n", err);
				break;
			}
		}
	}

	return 0;
}


#ifdef USE_TLS
static int cmd_tls_issuer(struct re_printf *pf, void *unused)
{
	int err = 0;
	struct mbuf *mb;
	(void)unused;

	mb = mbuf_alloc(20);
	if (!mb)
		return ENOMEM;

	err = tls_get_issuer(uag_tls(), mb);
	if (err == ENOENT) {
		(void)re_hprintf(pf, "sip_certificate not configured\n");
	}
	else if (err == ENOTSUP) {
		(void)re_hprintf(pf, "could not get issuer of configured "
				"certificate (%m)\n", err);
	}
	else if (err) {
		(void)re_hprintf(pf, "unable to print certificate issuer "
				"(%m)\n", err);
	}

	if (err)
		goto out;

	(void)re_hprintf(pf, "TLS Cert Issuer: %b\n", mb->buf, mb->pos);

 out:
	mem_deref(mb);
	return err;
}


static int cmd_tls_subject(struct re_printf *pf, void *unused)
{
	int err = 0;
	struct mbuf *mb;
	(void)unused;

	mb = mbuf_alloc(20);
	if (!mb)
		return ENOMEM;

	err = tls_get_subject(uag_tls(), mb);
	if (err == ENOENT) {
		(void)re_hprintf(pf, "sip_certificate not configured\n");
	}
	else if (err == ENOTSUP) {
		(void)re_hprintf(pf, "could not get subject of configured "
				"certificate (%m)\n", err);
	}
	else if (err) {
		(void)re_hprintf(pf, "unable to print certificate subject "
				 " (%m)\n", err);
	}

	if (err)
		goto out;

	(void)re_hprintf(pf, "TLS Cert Subject: %b\n", mb->buf, mb->pos);

 out:
	mem_deref(mb);
	return err;
}
#endif

/*Static call menu*/
static const struct cmd cmdv[] = {

{"about",     0,          0, "About box",               about_box            },
{"accept",    'a',        0, "Accept incoming call",    cmd_answer           },
{"acceptdir", 0,    CMD_PRM, "Accept incoming call with audio and video"
                             "direction.",              cmd_answerdir        },
{"answermode",0,    CMD_PRM, "Set answer mode",         cmd_set_answermode   },
{"auplay",    0,    CMD_PRM, "Switch audio player",     switch_audio_player  },
{"ausrc",     0,    CMD_PRM, "Switch audio source",     switch_audio_source  },
{"callstat",  'c',        0, "Call status",             ua_print_call_status },
{"dial",      'd',  CMD_PRM, "Dial",                    dial_handler         },
{"dialdir",   0,    CMD_PRM, "Dial with audio and video"
                             "direction.",              cmd_dialdir          },
{"dnd",       0,    CMD_PRM, "Set Do not Disturb",      cmd_dnd              },
{"hangup",    'b',        0, "Hangup call",             cmd_hangup           },
{"hangupall", 0,    CMD_PRM, "Hangup all calls with direction"
                                                       ,cmd_hangupall        },
{"help",      'h',        0, "Help menu",               print_commands       },
{"listcalls", 'l',        0, "List active calls",       cmd_print_calls      },
{"options",   'o',  CMD_PRM, "Options",                 options_command      },
{"reginfo",   'r',        0, "Registration info",       ua_print_reg_status  },
{"setadelay", 0,    CMD_PRM, "Set answer delay for outgoing call",
                                                        cmd_set_adelay       },
{"uadel",     0,    CMD_PRM, "Delete User-Agent",       cmd_ua_delete        },
{"uadelall",  0,    CMD_PRM, "Delete all User-Agents",  cmd_ua_delete_all    },
{"uafind",    0,    CMD_PRM, "Find User-Agent <aor>",   cmd_ua_find          },
{"uanew",     0,    CMD_PRM, "Create User-Agent",       create_ua            },
{"vidsrc",    0,    CMD_PRM, "Switch video source",     switch_video_source  },
{NULL,        KEYCODE_ESC,0, "Hangup call",             cmd_hangup           },

#ifdef USE_TLS
{"tlsissuer", 0,          0, "TLS certificate issuer",  cmd_tls_issuer       },
{"tlssubject",0,          0, "TLS certificate subject", cmd_tls_subject      },
#endif

};

static const struct cmd dialcmdv[] = {
/* Numeric keypad inputs: */
{NULL, '#', CMD_PRM, NULL,   dial_handler },
{NULL, '*', CMD_PRM, NULL,   dial_handler },
{NULL, '0', CMD_PRM, NULL,   dial_handler },
{NULL, '1', CMD_PRM, NULL,   dial_handler },
{NULL, '2', CMD_PRM, NULL,   dial_handler },
{NULL, '3', CMD_PRM, NULL,   dial_handler },
{NULL, '4', CMD_PRM, NULL,   dial_handler },
{NULL, '5', CMD_PRM, NULL,   dial_handler },
{NULL, '6', CMD_PRM, NULL,   dial_handler },
{NULL, '7', CMD_PRM, NULL,   dial_handler },
{NULL, '8', CMD_PRM, NULL,   dial_handler },
{NULL, '9', CMD_PRM, NULL,   dial_handler },
};


/**
 * Register static menu of baresip
 *
 * @return int 0 if success, errorcode otherwise
 */
int static_menu_register(void)
{
	return cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
}


/**
 * Unregister static menu of baresip
 */
void static_menu_unregister(void)
{
	cmd_unregister(baresip_commands(), cmdv);
}


/**
 * Register dial menu
 *
 * @return int 0 if success, errorcode otherwise
 */
int dial_menu_register(void)
{
	struct commands *baresip_cmd = baresip_commands();

	if (!cmds_find(baresip_cmd, dialcmdv))
		return cmd_register(baresip_cmd,
			dialcmdv, ARRAY_SIZE(dialcmdv));

	return 0;
}


/**
 * Unregister dial menu
 */
void dial_menu_unregister(void)
{
	cmd_unregister(baresip_commands(), dialcmdv);
}
