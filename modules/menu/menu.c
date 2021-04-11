/**
 * @file menu.c  Interactive menu
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <re.h>
#include <baresip.h>
#include "menu.h"


/**
 * @defgroup menu menu
 *
 * Interactive menu
 *
 * This module must be loaded if you want to use the interactive menu
 * to control the Baresip application.
 */
static struct menu menu;

enum {
	MIN_RINGTIME = 1000,
};


static int menu_set_incall(bool incall)
{
	int err = 0;

	/* Dynamic menus */
	if (incall) {
		dial_menu_unregister();
		err = dynamic_menu_register();
	}
	else {
		dynamic_menu_unregister();
		err = dial_menu_register();
	}
	if (err) {
		warning("menu: set_incall: cmd_register failed (%m)\n", err);
	}

	return err;
}


static void tmrstat_handler(void *arg)
{
	struct call *call;
	(void)arg;

	/* the UI will only show the current active call */
	call = menu_callcur();
	if (!call)
		return;

	tmr_start(&menu.tmr_stat, 100, tmrstat_handler, 0);

	if (ui_isediting(baresip_uis()))
		return;

	if (STATMODE_OFF != menu.statmode) {
		(void)re_fprintf(stderr, "%H\r", call_status, call);
	}
}


void menu_update_callstatus(bool incall)
{
	/* if there are any active calls, enable the call status view */
	if (incall)
		tmr_start(&menu.tmr_stat, 100, tmrstat_handler, 0);
	else
		tmr_cancel(&menu.tmr_stat);
}


static void redial_reset(void)
{
	tmr_cancel(&menu.tmr_redial);
	menu.current_attempts = 0;
}


static char *errorcode_fb_aufile(uint16_t scode)
{
	switch (scode) {

	case 404: return "notfound.wav";
	case 486: return "busy.wav";
	case 487: return NULL; /* ignore */
	default:  return "error.wav";
	}
}


static char *errorcode_key_aufile(uint16_t scode)
{
	switch (scode) {

	case 404: return "notfound_aufile";
	case 486: return "busy_aufile";
	case 487: return NULL; /* ignore */
	default:  return "error_aufile";
	}
}


static bool active_call_test(const struct call* call)
{
	return call_state(call) == CALL_STATE_ESTABLISHED &&
			!call_is_onhold(call);
}


static void find_first_call(struct call *call, void *arg)
{
	struct call **callp = arg;

	if (!*callp)
		*callp = call;
}


struct call *menu_find_call_state(enum call_state st)
{
	struct le *le;

	for (le = list_head(uag_list()); le; le = le->next) {
		struct ua *ua = le->data;
		struct call *call = ua_find_call_state(ua, st);

		if (call)
			return call;
	}

	return NULL;
}

/**
 * Search all User-Agents for a call that matches
 *
 * @param matchh  Optional match handler. If NULL, the last call of the first
 *                  User-Agent is returned
 *
 * @return  A call that matches
 */
struct call *menu_find_call(call_match_h *matchh)
{
	struct call *call = NULL;

	uag_filter_calls(find_first_call, matchh, &call);

	return call;
}


static void menu_stop_play(void)
{
	menu.play = mem_deref(menu.play);
	menu.ringback = false;
}


static void menu_play(const char *ckey, const char *fname, int repeat)
{
	struct config *cfg = conf_config();
	struct player *player = baresip_player();

	struct pl pl = PL_INIT;
	char *file = NULL;

	if (conf_get(conf_cur(), ckey, &pl))
		pl_set_str(&pl, fname);

	if (!pl_isset(&pl))
		return;

	pl_strdup(&file, &pl);
	menu_stop_play();
	(void)play_file(&menu.play, player, file, repeat,
			cfg->audio.play_mod,
			cfg->audio.play_dev);
	mem_deref(file);
}


static void play_incoming(const struct call *call)
{
	/* stop any ringtones */
	menu_stop_play();

	/* Only play the ringtones if answermode is "Manual".
	 * If the answermode is "auto" then be silent.
	 */
	if (ANSWERMODE_MANUAL == account_answermode(call_account(call))) {

		if (menu_find_call(active_call_test)) {
			menu_play("callwaiting_aufile", "callwaiting.wav", 3);
		}
		else {
			/* Alert user */
			menu_play("ring_aufile", "ring.wav", -1);
		}
	}
}


static void play_ringback(void)
{
	/* stop any ringtones */
	menu_stop_play();

	if (menu.ringback_disabled) {
		info("\nRingback disabled\n");
	}
	else {
		menu_play("ringback_aufile", "ringback.wav", -1);
		menu.ringback = true;
	}
}


static void play_resume(void)
{
	struct call *call = uag_call_find(menu.callid);

	switch (call_state(call)) {
	case CALL_STATE_INCOMING:
		play_incoming(call);
		break;
	case CALL_STATE_RINGING:
		if (!menu.ringback && !menu_find_call(active_call_test))
			play_ringback();
		break;
	default:
		break;
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

		if (!ua_isregistered(ua) && !account_prio(ua_account(ua)))
			return;
	}

	n = list_count(uag_list());

	/* We are ready */
	ui_output(baresip_uis(),
		  "\x1b[32mAll %u useragent%s registered successfully!"
		  " (%u ms)\x1b[;m\n",
		  n, n==1 ? "" : "s",
		  (uint32_t)(tmr_jiffies() - menu.start_ticks));

	ual_ready = true;
}


static void redial_handler(void *arg)
{
	char *uri = NULL;
	int err;
	(void)arg;

	info("now: redialing now. current_attempts=%u, max_attempts=%u\n",
	     menu.current_attempts,
	     menu.redial_attempts);

	if (menu.current_attempts > menu.redial_attempts) {

		info("menu: redial: too many attemptes -- giving up\n");
		return;
	}

	if (menu.dialbuf->end == 0) {
		warning("menu: redial: dialbuf is empty\n");
		return;
	}

	menu.dialbuf->pos = 0;
	err = mbuf_strdup(menu.dialbuf, &uri, menu.dialbuf->end);
	if (err)
		return;

	err = ua_connect(uag_find_aor(menu.redial_aor), NULL, NULL,
			 uri, VIDMODE_ON);
	if (err) {
		warning("menu: redial: ua_connect failed (%m)\n", err);
	}

	mem_deref(uri);
}


static void menu_play_closed(struct call *call)
{
	uint16_t scode;
	const char *key;
	const char *fb;

	/* stop any ringtones */
	menu_stop_play();

	if (call_scode(call)) {
		scode = call_scode(call);
		key = errorcode_key_aufile(scode);
		fb = errorcode_fb_aufile(scode);

		menu_play(key, fb, 1);
	}
}


static void auans_play_finished(struct play *play, void *arg)
{
	struct call *call = arg;
	int32_t adelay = call_answer_delay(call);
	(void) play;

	if (call_state(call) == CALL_STATE_INCOMING) {
		call_start_answtmr(call, adelay);
		if (adelay >= MIN_RINGTIME)
			play_incoming(call);
	}
}


static void start_sip_autoanswer(struct call *call)
{
	int32_t adelay = call_answer_delay(call);
	bool beep = true;

	if (adelay == -1)
		return;

	conf_get_bool(conf_cur(), "sip_autoanswer_beep", &beep);
	if (beep) {
		menu_play("sip_autoanswer_aufile", "autoanswer.wav", 1);
		play_set_finish_handler(menu.play, auans_play_finished, call);
	}
	else {
		call_start_answtmr(call, adelay);
		if (adelay >= MIN_RINGTIME)
			play_incoming(call);
	}
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct call *call2 = NULL;
	struct account *acc = ua_account(ua);
	int32_t adelay = -1;
	bool incall;
	enum sdp_dir ardir, vrdir;
	uint32_t count;
	int err;
	(void)prm;
	(void)arg;

#if 0
	debug("menu: [ ua=%s call=%s ] event: %s (%s)\n",
	      account_aor(acc), call_id(call), uag_event_str(ev), prm);
#endif


	count = uag_call_count();
	switch (ev) {

	case UA_EVENT_CALL_INCOMING:

		/* set the current User-Agent to the one with the call */
		menu_selcall(call);
		menu_stop_play();

		ardir =sdp_media_rdir(
			stream_sdpmedia(audio_strm(call_audio(call))));
		vrdir = sdp_media_rdir(
			stream_sdpmedia(video_strm(call_video(call))));
		if (!call_has_video(call))
			vrdir = SDP_INACTIVE;

		info("%s: Incoming call from: %s %s - audio-video: %s-%s -"
		     " (press 'a' to accept)\n",
		     account_aor(acc), call_peername(call), call_peeruri(call),
		     sdp_dir_name(ardir), sdp_dir_name(vrdir));

		if (acc && account_sip_autoanswer(acc))
			adelay = call_answer_delay(call);

		if (adelay == -1)
			play_incoming(call);
		else
			start_sip_autoanswer(call);

		break;

	case UA_EVENT_CALL_RINGING:
		menu_selcall(call);
		if (!menu.ringback && !menu_find_call(active_call_test))
			play_ringback();
		break;

	case UA_EVENT_CALL_PROGRESS:
		menu_selcall(call);
		menu_stop_play();
		break;

	case UA_EVENT_CALL_ANSWERED:
		menu_stop_play();
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		menu_selcall(call);
		/* stop any ringtones */
		menu_stop_play();

		/* We must stop the re-dialing if the call was
		   established */
		redial_reset();
		uag_hold_others(call);
		break;

	case UA_EVENT_CALL_CLOSED:
		/* Activate the re-dialing if:
		 *
		 * - redial_attempts must be enabled in config
		 * - the closed call must be of outgoing direction
		 * - the closed call must fail with special code 701
		 */
		if (menu.redial_attempts) {

			if (menu.current_attempts
			    ||
			    (call_is_outgoing(call) &&
			     call_scode(call) == 701)) {

				info("menu: call closed"
				     " -- redialing in %u seconds\n",
				     menu.redial_delay);

				++menu.current_attempts;

				str_ncpy(menu.redial_aor, account_aor(acc),
					 sizeof(menu.redial_aor));

				tmr_start(&menu.tmr_redial,
					  menu.redial_delay*1000,
					  redial_handler, NULL);
			}
			else {
				info("menu: call closed -- not redialing\n");
			}
		}

		if (!str_cmp(call_id(call), menu.callid)) {
			menu_play_closed(call);
			menu_selcall(NULL);
			play_resume();
		}

		break;

	case UA_EVENT_CALL_REMOTE_SDP:
		if (!str_cmp(prm, "answer") &&
				call_state(call) == CALL_STATE_ESTABLISHED)
			menu_selcall(call);
		break;

	case UA_EVENT_CALL_TRANSFER:
		/*
		 * Create a new call to transfer target.
		 *
		 * NOTE: we will automatically connect a new call to the
		 *       transfer target
		 */

		info("menu: transferring call %s to '%s'\n",
		     call_id(call), prm);

		err = ua_call_alloc(&call2, ua, VIDMODE_ON, NULL, call,
				    call_localuri(call), true);
		if (!err) {
			struct pl pl;

			pl_set_str(&pl, prm);

			err = call_connect(call2, &pl);
			if (err) {
				warning("ua: transfer: connect error: %m\n",
					err);
			}
		}

		if (err) {
			(void)call_notify_sipfrag(call, 500, "Call Error");
			mem_deref(call2);
		}
		break;

	case UA_EVENT_CALL_TRANSFER_FAILED:
		info("menu: transfer failure: %s\n", prm);
		break;

	case UA_EVENT_REGISTER_OK:
		check_registrations();
		break;

	case UA_EVENT_UNREGISTERING:
		return;

	case UA_EVENT_MWI_NOTIFY:
		info("----- MWI for %s -----\n", account_aor(acc));
		info("%s\n", prm);
		break;

	case UA_EVENT_AUDIO_ERROR:
		info("menu: audio error (%s)\n", prm);
		break;

	default:
		break;
	}

	incall = ev == UA_EVENT_CALL_CLOSED ? count > 1 : count;
	menu_set_incall(incall);
	menu_update_callstatus(incall);
}


static void message_handler(struct ua *ua, const struct pl *peer,
			    const struct pl *ctype,
			    struct mbuf *body, void *arg)
{
	struct config *cfg;
	(void)ua;
	(void)ctype;
	(void)arg;

	cfg = conf_config();

	ui_output(baresip_uis(), "\r%r: \"%b\"\n",
		  peer, mbuf_buf(body), mbuf_get_left(body));

	(void)play_file(NULL, baresip_player(), "message.wav", 0,
	                cfg->audio.alert_mod, cfg->audio.alert_dev);
}


/**
 * Get the menu object
 *
 * @return ptr to menu object
 */
struct menu *menu_get(void)
{
	return &menu;
}


/**
 * Selects the active call.
 *
 * @param call The call
 */
void menu_selcall(struct call *call)
{
	int i;
	enum call_state state[] = {
		CALL_STATE_INCOMING,
		CALL_STATE_OUTGOING,
		CALL_STATE_RINGING,
		CALL_STATE_EARLY,
		CALL_STATE_ESTABLISHED,
	};

	if (!call) {
		/* select another call */
		for (i = ARRAY_SIZE(state)-1; i >= 0; --i) {
			call = menu_find_call_state(state[i]);

			if (!str_cmp(call_id(call), menu.callid))
				call = NULL;

			if (call)
				break;
		}
	}

	menu.callid = mem_deref(menu.callid);

	if (call) {
		str_dup(&menu.callid, call_id(call));
		call_set_current(ua_calls(call_get_ua(call)), call);
	}
}


/**
 * Gets the active call.
 *
 * @return The active call.
 */
struct call *menu_callcur(void)
{
	return uag_call_find(menu.callid);
}


/**
 * Get UA of active call
 *
 * @return ptr to UA object
 */
struct ua *menu_uacur(void)
{
	return call_get_ua(menu_callcur());
}


/**
 * Manual selection of the UA via command parameter
 * - carg->data has highest priority
 * - otherwise second word in carg->prm is checked for an UA index
 *
 * @param pf    Print backend
 * @param carg  Command argument
 * @param word1 First word
 * @param word2 Second word
 *
 * @return The UA if found, NULL otherwise.
 */
struct ua   *menu_ua_carg(struct re_printf *pf, const struct cmd_arg *carg,
		struct pl *word1, struct pl *word2)
{
	int err;
	struct le *le;
	uint32_t i;
	struct ua *ua = carg->data;

	if (ua)
		return ua;

	err = re_regex(carg->prm, str_len(carg->prm), "[^ ]+ [^ ]+", word1,
			word2);
	if (err)
		return NULL;

	i = pl_u32(word2);

	le = uag_list()->head;
	while (le && i--)
		le = le->next;

	if (le) {
		ua = le->data;
		info("%s: selected for request\n",
				account_aor(ua_account(ua)));
	}
	else {
		re_hprintf(pf, "no User-Agent at pos %r\n", word2);
	}

	return ua;
}


/**
 * Decode a command parameter
 *
 * @param prm  Command arguments parameter string
 * @param name Parameter name
 * @param val  Returned parameter value
 *
 * @return 0 for success, otherwise errorcode
 */
int menu_param_decode(const char *prm, const char *name, struct pl *val)
{
	char expr[128];
	struct pl v;

	if (!str_isset(prm) || !name || !val)
		return EINVAL;

	(void)re_snprintf(expr, sizeof(expr),
			  "[ \t\r\n]*%s[ \t\r\n]*=[ \t\r\n]*[~ \t\r\n;]+",
			  name);

	if (re_regex(prm, strlen(prm), expr, NULL, NULL, NULL, &v))
		return ENOENT;

	*val = v;

	return 0;
}


static int module_init(void)
{
	struct pl val;
	int err;

	memset(&menu, 0, sizeof(menu));
	menu.redial_attempts = 0;
	menu.redial_delay = 5;
	menu.ringback_disabled = false;
	menu.statmode = STATMODE_CALL;
	menu.clean_number = false;
	menu.play = NULL;
	menu.adelay = -1;

	/*
	 * Read the config values
	 */
	conf_get_bool(conf_cur(), "ringback_disabled",
		      &menu.ringback_disabled);
	conf_get_bool(conf_cur(), "menu_clean_number", &menu.clean_number);

	if (0 == conf_get(conf_cur(), "redial_attempts", &val) &&
	    0 == pl_strcasecmp(&val, "inf")) {
		menu.redial_attempts = (uint32_t)-1;
	}
	else {
		conf_get_u32(conf_cur(), "redial_attempts",
			     &menu.redial_attempts);
	}
	conf_get_u32(conf_cur(), "redial_delay", &menu.redial_delay);

	if (menu.redial_attempts) {
		info("menu: redial enabled with %u attempts and"
		     " %u seconds delay\n",
		     menu.redial_attempts,
		     menu.redial_delay);
	}

	menu.dialbuf = mbuf_alloc(64);
	if (!menu.dialbuf)
		return ENOMEM;

	menu.start_ticks = tmr_jiffies();

	if (0 == conf_get(conf_cur(), "statmode_default", &val) &&
	    0 == pl_strcasecmp(&val, "off")) {
		menu.statmode = STATMODE_OFF;
	}
	else {
		menu.statmode = STATMODE_CALL;
	}

	err = static_menu_register();
	err |= dial_menu_register();
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	err = message_listen(baresip_message(),
			     message_handler, NULL);
	if (err)
		return err;

	return err;
}


static int module_close(void)
{
	debug("menu: close (redial current_attempts=%d)\n",
	      menu.current_attempts);

	message_unlisten(baresip_message(), message_handler);

	uag_event_unregister(ua_event_handler);
	static_menu_unregister();
	dial_menu_unregister();
	dynamic_menu_unregister();

	tmr_cancel(&menu.tmr_stat);
	menu.dialbuf = mem_deref(menu.dialbuf);
	menu.callid = mem_deref(menu.callid);
	menu_stop_play();

	tmr_cancel(&menu.tmr_redial);

	return 0;
}


const struct mod_export DECL_EXPORTS(menu) = {
	"menu",
	"application",
	module_init,
	module_close
};
