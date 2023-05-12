/**
 * @file commod.c Commend application module
 *
 * Copyright (C) 2020 Commend.com - c.spielberger@commend.com
 */

#include <re.h>
#include <baresip.h>


/**
 * @defgroup commod commod
 *
 * This module implements Commend specific commands
 */


#define DEBUG_MODULE "commod"
#define DEBUG_LEVEL 5
#include <re_dbg.h>

struct amle {
	struct le he;
	struct account *acc;
	enum answermode am;
};

struct commod {
	struct play *cur_play;
	struct call *cur_call;
	struct hash *answmod;
};


static struct commod d = { NULL, NULL, NULL };


/**
 * Print info for given call with Commend specific informations
 *
 * Printed the following line where the parameters are as follwing
 * %u %s %d %u %d %s %s %s
 * %u	line number
 * %s	call state
 * %d	outgoing call as bool 1 = outgoing, 0 = incoming
 * %u	call duration in seconds
 * %d	on hold as bool 1 = on hold, 0 = active
 * %s	id
 * %s	peer uri
 * %s	peer name
 *
 * @param pf	Print handler for debug output
 * @param call	call to print
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_call_info(struct re_printf *pf, const struct call *call)
{
	if (!call)
		return 0;

	return re_hprintf(pf, "%u %s %d %u %d %s %s %s",
			  call_linenum(call),
			  call_statename(call),
			  call_is_outgoing(call),
			  call_duration(call),
			  call_is_onhold(call),
			  call_id(call),
			  call_peeruri(call),
			  call_peername(call));
}

/**
 * Commend specific calls print
 *
 * @param pf     Print handler for debug output
 * @param ua     User-Agent
 *
 * @return 0 if success, otherwise errorcode
 */
static int com_ua_print_calls(struct re_printf *pf, const struct ua *ua)
{
	uint32_t n, count=0;
	uint32_t linenum;
	struct account *acc;
	struct uri *uri;
	int err = 0;

	if (!ua) {
		err |= re_hprintf(pf, "\n--- No active calls ---\n");
		return err;
	}

	acc = ua_account(ua);
	uri = account_luri(acc);
	n = list_count(ua_calls(ua));

	err |= re_hprintf(pf, "\nUser-Agent: %r@%r\n", &uri->user, &uri->host);
	err |= re_hprintf(pf, "--- Active calls (%u) ---\n", n);

	for (linenum = 1; linenum < 256; linenum++) {

		const struct call *call;

		call = call_find_linenum(ua_calls(ua), linenum);
		if (call) {
			++count;

			err |= re_hprintf(pf, "%s %H\n",
					  call == ua_call(ua) ? ">" : " ",
					  com_call_info, call);
		}

		if (count >= n)
			break;
	}

	err |= re_hprintf(pf, "\n");

	return err;
}


/**
 * Print all calls with Commend specific informations
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_print_calls(struct re_printf *pf, void *arg)
{
	struct le *le;
	int err;
	(void) arg;

	for (le = list_head(uag_list()); le; le = le->next) {
		const struct ua *ua = le->data;
		err = com_ua_print_calls(pf, ua);
		if (err)
			return err;
	}

	return 0;
}


static int param_decode(const char *prm, const char *name, struct pl *val)
{
	char expr[128];
	struct pl v;

	if (!str_isset(prm) || !name || !val)
		return EINVAL;

	(void)re_snprintf(expr, sizeof(expr),
			  "[ \t\r\n]*%s[ \t\r\n]*=[ \t\r\n]*[~ \t\r\n;]+",
			  name);

	if (re_regex(prm, str_len(prm), expr, NULL, NULL, NULL, &v))
		return ENOENT;

	*val = v;

	return 0;
}


const char *playmod_usage = "/com_playmod"
			    " source=<audiofile>"
			    " [player=<player_mod>,<player_dev>]\n";

static int cmd_playmod_file(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;

	struct pl src_param = PL_INIT;
	struct pl player_param = PL_INIT;

	struct pl mod_param = PL_INIT;
	struct pl dev_param = PL_INIT;

	struct config *cfg;

	char *alert_mod = NULL;
	char *alert_dev = NULL;
	char *filename = NULL;

	int err = 0;

	cfg = conf_config();

	/* Stop the current tone, if any */
	d.cur_play = mem_deref(d.cur_play);

	if (param_decode(carg->prm, "source", &src_param)) {
		re_hprintf(pf, "commod: No source defined.\n");
		goto out;
	}

	pl_strdup(&filename, &src_param);

	err = param_decode(carg->prm, "player", &player_param);
	if (!err) {
		if (!re_regex(player_param.p,
			     player_param.l, "[^,]+,[~]*",
			     &mod_param, &dev_param)) {

			pl_strdup(&alert_mod, &mod_param);

			if (pl_isset(&dev_param))
				pl_strdup(&alert_dev, &dev_param);
		}
	}
	else {
		str_dup(&alert_mod, cfg->audio.alert_mod);
		str_dup(&alert_dev, cfg->audio.alert_dev);
	}


	if (str_isset(filename)) {
		re_hprintf(pf, "playing audio file \"%s\" ..\n", filename);

		err = play_file(
			&d.cur_play, baresip_player(),
			filename, 0,  alert_mod, alert_dev);

		if (err) {
			warning("commod: play_file(%s) failed (%m)\n",
				filename, err);
			goto out;
		}
	}

out:

	if (err)
		(void) re_hprintf(pf, "usage: %s", playmod_usage);

	mem_deref(alert_mod);
	mem_deref( alert_dev);
	mem_deref(filename);

	return err;
}


/**
 * @brief Checks auto answer media direction account setting
 *
 * Account parameter:
 * ;extra=...,auto_audio=recvonly,auto_video=inactive
 *
 * Default is sendrecv. So in order to disable video only specify:
 * ;extra=...,auto_video=inactive
 *
 * @param call The incoming call
 */
static void check_auto_answer_media_direction(struct call *call)
{
	struct ua *ua;
	struct account *acc;
	bool autoanswer = false;

	if (!call)
		return;

	ua = call_get_ua(call);
	acc = ua_account(ua);

	autoanswer = account_answermode(acc) == ANSWERMODE_AUTO ||
		account_answerdelay(acc) ||
		(account_sip_autoanswer(acc) && call_answer_delay(call) != -1);

	if (autoanswer) {
		struct pl pl = PL_INIT;
		struct pl v = PL_INIT;
		enum sdp_dir adir = SDP_SENDRECV;
		enum sdp_dir vdir = SDP_SENDRECV;
		bool found = false;

		pl_set_str(&pl, account_extra(acc));
		if (fmt_param_sep_get(&pl, "auto_audio", ',', &v)) {
			adir = sdp_dir_decode(&v);
			found = true;
		}

		if (fmt_param_sep_get(&pl, "auto_video", ',', &v)) {
			vdir = sdp_dir_decode(&v);
			found = true;
		}

		if (found) {
			if (call_sent_answer(call))
				(void)call_set_media_estdir(call, adir, vdir);
			else
				(void)call_set_media_direction(call,
							       adir, vdir);
		}
	}
}


static void hangup_outgoing_ua(struct call *call, void *arg)
{
	struct ua *ua = arg;

	if (call_get_ua(call) != ua)
		return;

	if (call_state(call) != CALL_STATE_OUTGOING &&
	    call_state(call) != CALL_STATE_RINGING &&
	    call_state(call) != CALL_STATE_EARLY)
		return;

	ua_hangup(ua, call, 480, "Temporarily Unavailable");
}


static int acc_add_answmod(struct account *acc)
{
	struct amle *amle;

	amle = mem_zalloc(sizeof(*amle), NULL);
	if (!amle)
		return ENOMEM;

	amle->am  = account_answermode(acc);
	amle->acc = acc;
	hash_append(d.answmod, hash_fast_str(account_aor(acc)),
		    &amle->he, amle);
	return 0;
}


static bool amle_restore_applyh(struct le *le, void *arg)
{
	struct amle *amle = le->data;
	(void)arg;

	account_set_answermode(amle->acc, amle->am);
	return true;
}


static void acc_restore_answmods(void)
{
	hash_apply(d.answmod, amle_restore_applyh, NULL);
	hash_flush(d.answmod);
}


static bool amle_get_applyh(struct le *le, void *arg)
{
	struct amle *amle = le->data;
	enum answermode *amp = arg;

	*amp = amle->am;
	return true;
}


static enum answermode acc_answmod_get(const struct account *acc)
{
	enum answermode am = ANSWERMODE_MANUAL;

	hash_lookup(d.answmod, hash_fast_str(account_aor(acc)),
		    amle_get_applyh, &am);

	return am;
}


static void sel_oldest_call(struct call *call, void *arg)
{
	struct call *closed = arg;

	if (call == closed)
		return;

	if (call_state(d.cur_call) != CALL_STATE_ESTABLISHED &&
	    call_state(call)       == CALL_STATE_ESTABLISHED)
		d.cur_call = call;

	else if (!d.cur_call && call_state(call) == CALL_STATE_INCOMING)
		d.cur_call = call;
}


static void call_earlymedia_disable(struct call *call)
{
	if (call_refresh_allowed(call)) {
		call_set_audio_ldir(call, SDP_INACTIVE);
		call_set_video_ldir(call, SDP_INACTIVE);
		call_modify(call);
	}
}


static void call_earlymedia_enable(struct call *call)
{
	enum answermode am = acc_answmod_get(call_account(call));
	enum sdp_dir adir = am == ANSWERMODE_EARLY ? SDP_SENDRECV :
			    am == ANSWERMODE_EARLY_AUDIO ? SDP_RECVONLY :
			    SDP_INACTIVE;
	enum sdp_dir vdir = am == ANSWERMODE_EARLY ? SDP_SENDRECV :
			    am == ANSWERMODE_EARLY_VIDEO ? SDP_RECVONLY :
			    SDP_INACTIVE;

	if (adir == SDP_INACTIVE && vdir == SDP_INACTIVE)
		return;

	if (call_refresh_allowed(call)) {
		call_set_audio_ldir(call, adir);
		call_set_video_ldir(call, vdir);
		call_modify(call);
	}
	else {
		call_progress_dir(call, adir, vdir);
	}
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct account *acc = ua_account(ua);
	enum answermode am = account_answermode(acc);
	enum sdp_dir adir = sdp_media_rdir(stream_sdpmedia(audio_strm(
					   call_audio(call))));
	enum sdp_dir vdir = sdp_media_rdir(stream_sdpmedia(video_strm(
					   call_video(call))));
	bool video;
	bool audio;
	bool control;
	(void) arg;

	info("commod: [ ua=%s call=%s ] event: %s (%s)\n",
	     account_aor(acc), call_id(call), uag_event_str(ev), prm);

	video = (am == ANSWERMODE_EARLY || am == ANSWERMODE_EARLY_VIDEO) &&
		vdir & SDP_RECVONLY;
	audio = (am == ANSWERMODE_EARLY || am == ANSWERMODE_EARLY_AUDIO) &&
		adir & SDP_RECVONLY;
	control = video || audio;
	switch (ev) {
		case UA_EVENT_CALL_INCOMING:
			check_auto_answer_media_direction(call);
			if (control && !d.cur_call) {
				d.cur_call = call;
			}

			if (d.cur_call != call) {
				if (am != ANSWERMODE_MANUAL)
					acc_add_answmod(acc);

				account_set_answermode(acc, ANSWERMODE_MANUAL);
			}

			/*@fallthrough@*/
		case UA_EVENT_CALL_OUTGOING:
			d.cur_play = mem_deref(d.cur_play);
			break;
		case UA_EVENT_REGISTER_FAIL:
			/* SYFU-942: hangup all call-requests */
			uag_filter_calls(hangup_outgoing_ua, NULL, ua);
			break;
		case UA_EVENT_CALL_ESTABLISHED:
		case UA_EVENT_CALL_ANSWERED:
			if (call_is_outgoing(call))
			    break;

			if (d.cur_call != call &&
			    call_state(d.cur_call) == CALL_STATE_INCOMING) {
				call_set_video_dir(d.cur_call, SDP_INACTIVE);
			}

			d.cur_call = call;
			break;
		case UA_EVENT_CALL_CLOSED:
			if (d.cur_call == call) {
				d.cur_call = NULL;
				uag_filter_calls(sel_oldest_call, NULL, call);
				if (d.cur_call)
					call_earlymedia_enable(d.cur_call);
				else
					acc_restore_answmods();
			}
			break;
		default:
			break;
	}
}


static void find_first_call(struct call *call, void *arg)
{
	struct call **ret = arg;

	*ret = call;
}


static struct call *current_call(void)
{
	struct call *call = NULL;

	uag_filter_calls(find_first_call, NULL, &call);
	return call;
}


/**
 * Removes the current audio codec from local SDP
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_rm_aucodec(struct re_printf *pf, void *arg)
{
	struct call *call = current_call();
	struct stream *stream;
	struct sdp_media *m;
	struct sdp_format *f;
	(void) arg;

	if (!call)
		return EINVAL;

	stream = audio_strm(call_audio(call));
	m = stream_sdpmedia(stream);
	f = sdp_media_format(m, true, NULL, -1, NULL, -1, -1);

	if (f)
		re_hprintf(pf, "Removing SDP format:\n%H\n", sdp_format_debug,
			   f);
	else
		re_hprintf(pf, "No SDP format found\n");

	mem_deref(f);
	return 0;
}


/**
 * Switch early media to given incoming call
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_switch_earlymedia(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;

	struct call *call;
	const char *usage = "usage: /com_switchearly <callid>\n";

	if (!str_isset(carg->prm)) {
		(void) re_hprintf(pf, "%s", usage);
		return EINVAL;
	}

	if (!d.cur_call || call_state(d.cur_call) != CALL_STATE_INCOMING) {
		(void) re_hprintf(pf, "No incoming call or "
				  "established call\n");
		return EINVAL;
	}

	call = uag_call_find(carg->prm);
	if (!call) {
		(void) re_hprintf(pf, "Could not find call %s\n", carg->prm);
		return EINVAL;
	}

	if (call == d.cur_call)
		return 0;

	if (call_state(call) != CALL_STATE_INCOMING) {
		(void) re_hprintf(pf, "Call %s has state %s\n",
				  carg->prm, call_statename(call));
		return EINVAL;
	}

	call_earlymedia_disable(d.cur_call);
	call_earlymedia_enable(call);

	d.cur_call = call;
	return 0;
}


static const struct cmd cmdv[] = {

{"com_listcalls", 0, 0,	"List active calls Commend format", com_print_calls},
{"com_playmod",   0, CMD_PRM,	"Play audio file on audio player",
	cmd_playmod_file},
{"com_rmaucodec", 0, 0, "Remove current audio codec", com_rm_aucodec},
{"com_switchearly", 0, 0, "Switch early media to other incoming call",
	com_switch_earlymedia},
};


static int module_init(void)
{
	int err;

	err = uag_event_register(ua_event_handler, NULL);
	err |= cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	err |= hash_alloc(&d.answmod, 32);

	return err;
}


static int module_close(void)
{
	uag_event_unregister(ua_event_handler);
	cmd_unregister(baresip_commands(), cmdv);
	hash_flush(d.answmod);
	mem_deref(d.answmod);

	return 0;
}


const struct mod_export DECL_EXPORTS(commod) = {
	"commod",
	"application",
	module_init,
	module_close
};
