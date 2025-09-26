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
	TONE_DELAY   =   20,         /* Delay for starting tone in [ms]      */
};


struct filter_arg {
	enum call_state state;
	const struct call *exclude;
	const struct call *match;
	struct call *call;
};


static void menu_sel_other(struct call *exclude);

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
	(void)arg;

	/* the UI will only show the current current call */
	if (!menu.curcall)
		return;

	tmr_start(&menu.tmr_stat, 100, tmrstat_handler, 0);

	if (ui_isediting(baresip_uis()))
		return;

	if (STATMODE_OFF != menu.statmode) {
		(void)re_fprintf(stderr, "%H\r", call_status, menu.curcall);
	}
}


void menu_update_callstatus(bool incall)
{
	/* if there are any active calls, enable the call status view */
	if (incall && menu_callcur())
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
	case 486:
	case 603: return "busy.wav";
	case 487: return NULL; /* ignore */
	default:  return "error.wav";
	}
}


static char *errorcode_key_aufile(uint16_t scode)
{
	switch (scode) {

	case 404: return "notfound_aufile";
	case 486:
	case 603:
		  return "busy_aufile";
	case 487: return NULL; /* ignore */
	default:  return "error_aufile";
	}
}


static void limit_earlymedia(struct call* call, void *arg)
{
	enum sdp_dir ldir, ndir;
	uint32_t maxcnt = 32;
	bool update = false;
	(void)arg;

	if (!call_is_outgoing(call))
		return;

	ldir = sdp_media_ldir(stream_sdpmedia(audio_strm(call_audio(call))));
	ndir = ldir;
	conf_get_u32(conf_cur(), "menu_max_earlyaudio", &maxcnt);

	if (menu.outcnt > maxcnt)
		ndir = SDP_INACTIVE;
	else if (menu.outcnt > 1)
		ndir &= SDP_SENDONLY;

	if (ndir != ldir) {
		call_set_audio_ldir(call, ndir);
		update = true;
	}

	if (!call_video(call)) {
		if (update)
			call_update_media(call);

		return;
	}

	/* video */
	ldir = sdp_media_ldir(stream_sdpmedia(video_strm(call_video(call))));
	ndir = ldir;

	maxcnt = 32;
	conf_get_u32(conf_cur(), "menu_max_earlyvideo_rx", &maxcnt);
	if (menu.outcnt > maxcnt)
		ndir &= SDP_SENDONLY;

	maxcnt = 32;
	conf_get_u32(conf_cur(), "menu_max_earlyvideo_tx", &maxcnt);
	if (menu.outcnt > maxcnt)
		ndir &= SDP_RECVONLY;

	if (ndir != ldir) {
		call_set_video_ldir(call, ndir);
		update = true;
	}

	if (update)
		call_update_media(call);
}


static bool active_call_test(const struct call* call, void *arg)
{
	struct filter_arg *fa = arg;

	if (call == fa->exclude)
		return false;

	return call_state(call) == CALL_STATE_ESTABLISHED &&
			!call_is_onhold(call);
}


static bool established_call_test(const struct call* call, void *arg)
{
	struct filter_arg *fa = arg;

	if (call == fa->exclude)
		return false;

	return call_state(call) == CALL_STATE_ESTABLISHED;
}


static bool outgoing_call_test(const struct call* call, void *arg)
{
	struct filter_arg *fa = arg;
	enum call_state st;

	if (call == fa->exclude)
		return false;

	st = call_state(call);
	return  st == CALL_STATE_OUTGOING || st == CALL_STATE_RINGING ||
		st == CALL_STATE_EARLY;
}


static void find_first_call(struct call *call, void *arg)
{
	struct filter_arg *fa = arg;

	if (!fa->call)
		fa->call = call;
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
 * @param exclude Call to exclude
 *
 * @return  A call that matches
 */
struct call *menu_find_call(call_match_h *matchh, const struct call *exclude)
{
	struct filter_arg fa = {CALL_STATE_UNKNOWN, exclude, NULL, NULL};

	uag_filter_calls(find_first_call, matchh, &fa);
	return fa.call;
}


static void menu_stop_play(void)
{
	menu.play = mem_deref(menu.play);
	menu.ringback = false;
	tmr_cancel(&menu.tmr_play);
}


static int menu_ovkey(char **key, const struct call *call, struct pl *suffix)
{
	int err;
	struct mbuf *mb = mbuf_alloc(32);
	if (!mb)
		return ENOMEM;

	err = mbuf_printf(mb, "%s-%r", call_id(call), suffix);
	if (err)
		goto out;

	mb->pos = 0;
	err = mbuf_strdup(mb, key, mb->end);
out:
	mem_deref(mb);
	return err;
}


static int menu_ovkey_str(char **key, const struct call *call,
			  const char *suffix)
{
	struct pl pl;
	pl_set_str(&pl, suffix);
	return menu_ovkey(key, call, &pl);
}

enum Device {
	DEVICE_ALERT,
	DEVICE_PLAYER
};

static bool menu_play(const struct call *call,
		      const char *ckey,
		      const char *fname,
		      int repeat,
		      enum Device device)
{
	struct config *cfg = conf_config();
	struct player *player = baresip_player();
	char *ovkey;
	const char *ovaukey = NULL;
	struct pl pl = PL_INIT;
	char *file = NULL;
	int err;
	const char *play_mod = cfg->audio.alert_mod;
	const char *play_dev = cfg->audio.alert_dev;

	if (device == DEVICE_PLAYER) {
		play_mod = cfg->audio.play_mod;
		play_dev = cfg->audio.play_dev;
	}

	if (ckey) {
		err = menu_ovkey_str(&ovkey, call, ckey);
		if (!err) {
			ovaukey = odict_string(menu.ovaufile, ovkey);
			mem_deref(ovkey);
		}

		if (ovaukey && !strcmp(ovaukey, "none"))
			return false;

		if (ovaukey)
			(void)conf_get(conf_cur(), ovaukey, &pl);

		if (!pl_isset(&pl))
			(void)conf_get(conf_cur(), ckey, &pl);
	}

	if (!pl_isset(&pl))
		pl_set_str(&pl, fname);

	if (!pl_isset(&pl) || !pl_strcmp(&pl, "none"))
		return false;

	pl_strdup(&file, &pl);
	menu_stop_play();
	err = play_file(&menu.play, player, file, repeat,
			play_mod,
			play_dev);
	mem_deref(file);

	return err == 0;
}


static void play_incoming(const struct call *call)
{

	if (call_state(call) != CALL_STATE_INCOMING)
		return;

	if (menu_find_call(active_call_test, call)) {
		menu_play(call, "callwaiting_aufile", "callwaiting.wav", 3,
			  DEVICE_PLAYER);
	}
	else if (menu.curcall == call) {
		/* Alert user */
		menu_play(call, "ring_aufile", "ring.wav", -1, DEVICE_ALERT);
	}
}


static void play_ringback(const struct call *call)
{
	/* stop any ringtones */
	menu_stop_play();

	if (menu.ringback_disabled) {
		info("menu: ringback disabled\n");
	}
	else {
		menu_play(call, "ringback_aufile", "ringback.wav", -1,
			  DEVICE_PLAYER);
		menu.ringback = true;
	}
}


static void check_ringback(struct call *call)
{
	enum sdp_dir adir = sdp_media_dir(stream_sdpmedia(
					  audio_strm(call_audio(call))));
	bool ring = !(adir & SDP_RECVONLY);

	if (ring && !menu.ringback &&
	    !menu_find_call(active_call_test, NULL)) {
		play_ringback(call);
	}
	else if (!ring) {
		menu_stop_play();
	}
}


static void delayed_play(void *arg)
{
	struct call *call = menu_callcur();
	(void) arg;

	switch (call_state(call)) {
	case CALL_STATE_INCOMING:
		play_incoming(call);
		break;
	case CALL_STATE_RINGING:
	case CALL_STATE_EARLY:
		check_ringback(call);
		break;
	default:
		menu_stop_play();
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

	info("menu: redialing now. current_attempts=%u, max_attempts=%u\n",
	     menu.current_attempts,
	     menu.redial_attempts);

	if (menu.current_attempts > menu.redial_attempts) {

		info("menu: redial: too many attempts -- giving up\n");
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


static void invite_handler(void *arg)
{
	(void)arg;

	const char *uri = menu.invite_uri;

	if (!str_isset(uri))
		return;

	int err;
	err = ua_connect(uag_find_requri(uri), NULL, NULL, uri, VIDMODE_ON);
	if (err)
		warning("menu: call to %s failed (%m)\n", menu.invite_uri,
			err);

	menu.invite_uri = mem_deref(menu.invite_uri);
}


static void menu_invite(const char *prm)
{
	menu.invite_uri = mem_deref(menu.invite_uri);
	int err = str_dup(&menu.invite_uri, prm);
	if (err) {
		warning("menu: call to %s failed (%m)\n", prm, err);
		return;
	}

	tmr_start(&menu.tmr_invite, 0, invite_handler, NULL);
}


static int menu_autoanwser_call(struct call *call)
{
	struct call *outgoing;
	if (menu_find_call(established_call_test, call))
		return EINVAL;

	outgoing = menu_find_call(outgoing_call_test, call);
	if (outgoing) {
		call_hangup(outgoing, 0, NULL);
		bevent_call_emit(BEVENT_CALL_CLOSED, outgoing,
				 "Outgoing call cancelled due to auto answer");
		mem_deref(outgoing);
	}

	return call_answer(call, 200, VIDMODE_ON);
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

		menu_play(call, key, fb, 1, DEVICE_ALERT);
	}
	else {
		menu_play(call, "hangup_aufile", "none", 0, DEVICE_PLAYER);
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


static bool alert_uri_supported(const char *uri)
{
	if (!re_regex(uri, strlen(uri), "https://"))
		return true;

	if (!re_regex(uri, strlen(uri), "http://"))
		return true;

	if (!re_regex(uri, strlen(uri), "file://") && fs_isfile(uri + 7))
		return true;

	return false;
}


static void start_autoanswer(struct call *call)
{
	struct account *acc = call_account(call);
	int32_t adelay = call_answer_delay(call);
	const char *aluri = call_alerturi(call);
	enum sipansbeep bmet = account_sipansbeep(acc);
	bool beep = false;

	if (adelay == -1)
		return;

	if (bmet) {
		if (bmet != SIPANSBEEP_LOCAL && aluri &&
				alert_uri_supported(aluri))
			beep = menu_play(call, NULL, aluri, 1, DEVICE_ALERT);

		if (!beep)
			beep = menu_play(call, "sip_autoanswer_aufile",
					 "autoanswer.wav", 1, DEVICE_ALERT);
	}

	if (beep) {
		play_set_finish_handler(menu.play, auans_play_finished, call);
	}
	else {
		call_start_answtmr(call, adelay);
		if (adelay >= MIN_RINGTIME)
			play_incoming(call);
	}
}


static bool ovaufile_del(struct le *le, void *arg)
{
	struct odict_entry *oe = le->data;
	struct call *call = arg;
	const char *id = call_id(call);

	if (!strncmp(odict_entry_key(oe), id, str_len(id)))
		mem_deref(oe);

	return false;
}


static void process_module_event(struct call *call, const char *prm)
{
	int err;
	struct pl module, event, data;
	struct pl from, to;
	char *ovkey;

	err = re_regex(prm, strlen(prm), "[^,]*,[^,]*,[~]*",
					 &module, &event, &data);
	if (err)
		return;

	if (!pl_strcmp(&event, "override-aufile")) {
		err = re_regex(data.p, data.l, "[^:]*:[~]*", &from, &to);
		if (err)
			return;

		err = menu_ovkey(&ovkey, call, &from);
		if (err)
			return;

		odict_entry_del(menu.ovaufile, ovkey);
		odict_entry_add(menu.ovaufile, ovkey, ODICT_STRING, to.p);
		mem_deref(ovkey);
	}
}


static void apply_contact_mediadir(struct call *call)
{
	const char *peeruri = call_peeruri(call);
	if (!peeruri)
		return;

	const struct contacts *contacts = baresip_contacts();
	struct contact *con = contact_find(contacts, peeruri);
	if (!con)
		return;

	enum sdp_dir caudir  = SDP_SENDRECV;
	enum sdp_dir cviddir = SDP_SENDRECV;
	contact_get_ldir(con, &caudir, &cviddir);

	enum sdp_dir estaudir  = SDP_SENDRECV;
	enum sdp_dir estviddir = SDP_SENDRECV;
	call_get_media_estdir(call, &estaudir, &estviddir);

	enum sdp_dir audir  = estaudir & caudir;
	enum sdp_dir viddir = estviddir & cviddir;
	if (audir == estaudir && viddir == estviddir)
		return;

	debug("menu: apply contact media direction audio=%s video=%s\n",
	      sdp_dir_name(audir), sdp_dir_name(viddir));
	call_set_media_direction(call, audir, viddir);
}


static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct call *call2 = NULL;
	int32_t adelay = -1;
	bool incall;
	enum sdp_dir ardir, vrdir;
	uint32_t count;
	struct pl val;
	char * uri;
	const char           *prm  = bevent_get_text(event);
	struct call          *call = bevent_get_call(event);
	struct ua            *ua   = bevent_get_ua(event);
	const struct sip_msg *msg  = bevent_get_msg(event);
	struct account       *acc  = ua_account(bevent_get_ua(event));
	int err;
	(void)arg;

#if 0
	debug("menu: [ ua=%s call=%s ] event: %s (%s)\n",
	      account_aor(acc), call_id(call), bevent_str(ev), prm);
#endif


	ardir =sdp_media_rdir(
			stream_sdpmedia(audio_strm(call_audio(call))));
	count = uag_call_count();

	switch (ev) {

	case BEVENT_SIPSESS_CONN:

		if (menu.dnd) {
			const uint16_t scode = 480;
			const char *reason = "Temporarily Unavailable";

			(void)sip_treply(NULL, uag_sip(), msg, scode, reason);

			info("menu: incoming call from %r <%r> rejected: "
			     "%u %s\n",
			     &msg->from.dname, &msg->from.auri, scode, reason);
			bevent_sip_msg_emit(BEVENT_MODULE, msg,
				"menu,rejected,%u %s", scode, reason);
			bevent_stop(event);
			break;
		}

		ua = uag_find_msg(msg);
		err = ua_accept(ua, msg);
		if (err) {
			warning("menu: could not accept incoming call (%m)\n",
				err);
			return;
		}

		bevent_stop(event);
		return;

	case BEVENT_CALL_INCOMING:

		apply_contact_mediadir(call);
		if (call_state(call) != CALL_STATE_INCOMING)
			return;

		if (account_answermode(acc) == ANSWERMODE_AUTO) {
			if (!menu_autoanwser_call(call))
				return;
		}

		/* the new incoming call should not change the current call */
		if (!menu.curcall)
			menu_selcall(call);
		else
			menu_selcall(menu.curcall);

		vrdir = sdp_media_rdir(
			stream_sdpmedia(video_strm(call_video(call))));
		if (!call_has_video(call))
			vrdir = SDP_INACTIVE;

		info("menu: %s: Incoming call from: %s %s - audio-video: %s-%s"
		     " - (press 'a' to accept)\n",
		     account_aor(acc), call_peername(call), call_peeruri(call),
		     sdp_dir_name(ardir), sdp_dir_name(vrdir));

		if (account_sip_autoanswer(acc)) {
			adelay = call_answer_delay(call);
		}
		else if (account_answerdelay(acc)) {
			adelay = account_answerdelay(acc);
			call_set_answer_delay(call, adelay);
		}

		if (adelay == -1)
			play_incoming(call);
		else
			start_autoanswer(call);

		break;

	case BEVENT_CALL_OUTGOING:
		apply_contact_mediadir(call);
		++menu.outcnt;
		break;

	case BEVENT_CALL_LOCAL_SDP:
		if (call_state(call) == CALL_STATE_OUTGOING)
			menu_selcall(call);
		break;

	case BEVENT_CALL_RINGING:
		menu_selcall(call);
		if (!menu.ringback && !menu_find_call(active_call_test, call))
			play_ringback(call);
		break;

	case BEVENT_CALL_PROGRESS:
		menu_selcall(call);
		uag_filter_calls(limit_earlymedia, NULL, NULL);

		tmr_start(&menu.tmr_play, TONE_DELAY, delayed_play, NULL);
		break;

	case BEVENT_CALL_ANSWERED:
		menu_stop_play();
		break;

	case BEVENT_CALL_ESTABLISHED:
		menu_selcall(call);
		/* stop any ringtones */
		menu_stop_play();

		/* We must stop the re-dialing if the call was established */
		redial_reset();
		uag_hold_others(call);
		break;

	case BEVENT_CALL_CLOSED:
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

		if (menu.xfer_call == call || menu.xfer_targ == call) {
			call_hold(menu.xfer_call, false);
			menu.xfer_call = NULL;
			menu.xfer_targ = NULL;
		}

		if (call == menu.curcall) {
			menu.curcall = NULL;
			if (count == 1) {
				menu_play_closed(call);
			}
			else {
				menu_sel_other(call);
				tmr_start(&menu.tmr_play, 0,
					  delayed_play, NULL);
			}
		}
		else if (call_state(call) == CALL_STATE_ESTABLISHED) {
			tmr_start(&menu.tmr_play, 0,
				  delayed_play, NULL);
		}

		hash_apply(menu.ovaufile->ht, ovaufile_del, call);
		if (call_is_outgoing(call))
			--menu.outcnt;

		break;

	case BEVENT_CALL_REMOTE_SDP:
		if (!str_cmp(prm, "answer") &&
				call_state(call) == CALL_STATE_ESTABLISHED)
			menu_selcall(call);

		if (call_state(call) == CALL_STATE_EARLY)
			tmr_start(&menu.tmr_play, TONE_DELAY, delayed_play,
				  NULL);

		break;

	case BEVENT_CALL_TRANSFER:
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

			call_set_user_data(call2, call_user_data(call));
			pl_set_str(&pl, prm);

			err = call_connect(call2, &pl);
			if (err) {
				warning("menu: transfer: connect error: %m\n",
					err);
			}
			else {
				module_event("menu", "transfer", ua, call,
					     "target %s", call_id(call2));
			}
		}

		if (err) {
			(void)call_notify_sipfrag(call, 500, "Call Error");
			mem_deref(call2);
		}
		break;

	case BEVENT_CALL_TRANSFER_FAILED:
		info("menu: transfer failure: %s\n", prm);
		menu_stop_play();
		call_hold(call, false);
		menu_selcall(call);
		break;

	case BEVENT_CALL_REDIRECT:
		uri = strchr(prm, ',');
		if (!uri)
			break;

		++uri;
		if (account_sip_autoredirect(ua_account(ua))) {
			info("menu: redirecting call to %s\n", uri);
			menu_invite(uri);
		}
		else {
			info("menu: redirect call to %s\n", uri);
		}
		break;

	case BEVENT_REFER:
		val = pl_null;
		if (!re_regex(prm, strlen(prm), "sip:"))
			pl_set_str(&val, "invite");

		(void)menu_param_decode(prm, "method", &val);
		if (!pl_strcmp(&val, "invite")) {
			info("menu: incoming REFER to %s\n", prm);
			menu_invite(prm);
		}

		break;

	case BEVENT_REGISTER_OK:
		check_registrations();
		break;

	case BEVENT_UNREGISTERING:
		return;

	case BEVENT_MWI_NOTIFY:
		info("menu: ----- MWI for %s -----\n", account_aor(acc));
		info("%s\n", prm);
		break;

	case BEVENT_AUDIO_ERROR:
		info("menu: audio error (%s)\n", prm);
		break;

	case BEVENT_MODULE:
		process_module_event(call, prm);
		break;

	default:
		break;
	}

	incall = ev == BEVENT_CALL_CLOSED ? count > 1 : count;
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

	if (menu.message_tone) {
		(void)play_file(NULL, baresip_player(), "message.wav", 0,
				cfg->audio.alert_mod, cfg->audio.alert_dev);
	}
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


static bool filter_call(const struct call *call, void *arg)
{
	struct filter_arg *fa = arg;

	if (fa->state != CALL_STATE_UNKNOWN && call_state(call) != fa->state)
		return false;

	if (call == fa->exclude)
		return false;

	if (fa->match && call != fa->match)
		return false;

	return true;
}


/**
 * Selects the given call to be the current call.
 *
 * @param call The call
 */
void menu_selcall(struct call *call)
{
	menu.curcall = call;
	call_set_current(ua_calls(call_get_ua(call)), call);
}


/**
 * Chooses a new current call.
 * Prefer call state established before early, ringing, outgoing and incoming
 */
static void menu_sel_other(struct call *exclude)
{
	int i;
	struct filter_arg fa = {CALL_STATE_UNKNOWN, exclude, NULL, NULL};
	enum call_state state[] = {
		CALL_STATE_INCOMING,
		CALL_STATE_OUTGOING,
		CALL_STATE_RINGING,
		CALL_STATE_EARLY,
		CALL_STATE_ESTABLISHED,
	};

	/* select another call */
	for (i = RE_ARRAY_SIZE(state)-1; i >= 0; --i) {
		fa.state = state[i];
		uag_filter_calls(find_first_call, filter_call, &fa);

		if (fa.call)
			break;
	}

	menu_selcall(fa.call);
}


/**
 * Gets the active call.
 *
 * @return The active call.
 */
struct call *menu_callcur(void)
{
	struct filter_arg fa = {CALL_STATE_UNKNOWN, NULL, menu.curcall, NULL};

	if (!menu.curcall)
		return NULL;

	uag_filter_calls(find_first_call, filter_call, &fa);
	return fa.call;
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

	if (ua) {
		pl_set_str(word1, carg->prm);
		return ua;
	}

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
		info("menu: %s: selected for request\n",
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


/**
 * Find ua and call from command arguments.
 *
 * Assumes that the first argument passed in carg->prm is a valid call-id
 * (if passed at all). If carg->data is set, it MUST be a pointer to a
 * struct ua. If no call-id is passed in carg->prm, the currently active call
 * is returned, if there is an active call.
 *
 * @param pf     Print backend
 * @param carg   Command arguments
 * @param uap    Pointer-pointer to ua. Set on successful return.
 * @param callp  Pointer-pointer to call. Set on successful return.
 *
 * @return 0 for success, otherwise errorcode
 */
int menu_get_call_ua(struct re_printf *pf, const struct cmd_arg *carg,
		     struct ua **uap, struct call **callp)
{
	int err = 0;
	struct ua *ua;
	struct call *call;
	const char *eq;
	char *cid = NULL;
	struct pl pl = PL_INIT;

	if (!carg || !uap || !callp)
		return EINVAL;

	/* fallback */
	ua = carg->data ? carg->data : menu_uacur();
	call = ua_call(ua);

	if (re_regex(carg->prm, str_len(carg->prm), "[^ ]+", &pl))
		goto out;

	/* A call-id MUST NOT contain an '='. See RFC 3261 section 25.1. */
	eq = pl_strchr(&pl, '=');
	if (eq)
		goto out;

	err = pl_strdup(&cid, &pl);
	if (err)
		return err;

	call = uag_call_find(cid);
	if (!call) {
		(void)re_hprintf(pf, "call %s not found\n", cid);
		err = EINVAL;
		goto out;
	}

	ua = call_get_ua(call);

out:
	if (!call && !err) {
		(void)re_hprintf(pf, "no active call\n");
		err = ENOENT;
	}

	if (!err) {
		*uap = ua;
		*callp = call;
	}

	mem_deref(cid);

	return err;
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
	menu.message_tone = true;
	err = odict_alloc(&menu.ovaufile, 8);
	if (err)
		return err;

	/*
	 * Read the config values
	 */
	conf_get_bool(conf_cur(), "ringback_disabled",
		      &menu.ringback_disabled);
	conf_get_bool(conf_cur(), "menu_clean_number", &menu.clean_number);
	conf_get_bool(conf_cur(), "menu_message_tone", &menu.message_tone);

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

	err = bevent_register(event_handler, NULL);
	if (err)
		return err;

	err = message_listen(baresip_message(),
			     message_handler, NULL);
	if (err)
		return err;

	return 0;
}


static int module_close(void)
{
	debug("menu: close (redial current_attempts=%d)\n",
	      menu.current_attempts);

	message_unlisten(baresip_message(), message_handler);

	bevent_unregister(event_handler);
	static_menu_unregister();
	dial_menu_unregister();
	dynamic_menu_unregister();

	tmr_cancel(&menu.tmr_stat);
	menu.dialbuf = mem_deref(menu.dialbuf);
	menu.invite_uri = mem_deref(menu.invite_uri);
	menu.ovaufile = mem_deref(menu.ovaufile);
	menu.ansval = mem_deref(menu.ansval);
	menu_stop_play();

	tmr_cancel(&menu.tmr_redial);
	tmr_cancel(&menu.tmr_invite);

	return 0;
}


const struct mod_export DECL_EXPORTS(menu) = {
	"menu",
	"application",
	module_init,
	module_close
};
