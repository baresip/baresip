/**
 * @file commend_commands.c  Commend specific commands
 *
 * Copyright (C) 2018 Commend.com
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <re.h>
#include <baresip.h>

#include "menu.h"

/**
 * @defgroup commend_commands commend_commands
 *
 * Commend specific commands
 *
 * This module must be loaded if you want to use commend specific commands
 * used by bct-inp to communicate with baresip.
 */


#define MAX_LINE_NBR 256

enum reg_status_t {
	STATUS_DISABLED = 0,
	STATUS_NOT_REGISTERED,
	STATUS_REGISTERED,
	STATUS_REACHABLE
};

struct ua_time {
	struct le le;
	uint64_t reg_time;
	struct ua *p_ua;
};

struct sip_log_t sip_log;
static struct list ua_reg_times = LIST_INIT;
struct play *play = NULL;
static bool mute = false;

/**
 * Get if global SIP ca is set
 *
 * @param pf		Print handler for debug output.
 * @param arg		Command argument: unused
 *
 * @return	 0	on success
 */
static int com_sip_ca(struct re_printf *pf, void *unused)
{
	int err;

	(void)unused;

	err = re_hprintf(pf, "SIP CA %sset\n",
			strlen(conf_config()->sip.cafile) > 0 ? "" : "not ");

	return err;
}


/**
 * Get if global SIP certificate is set
 *
 * @param pf		Print handler for debug output.
 * @param arg		Command argument: unused
 *
 * @return	 0	on success
 */
static int com_sip_certificate(struct re_printf *pf, void *unused)
{
	int err;

	(void)unused;

	err = re_hprintf(pf, "SIP certificate %sset\n",
			strlen(conf_config()->sip.cert) > 0 ? "" : "not ");

	return err;
}


/**
 * Get the name of the codec used by the current call
 *
 * @param pf		Print handler for debug output.
 * @param arg		Command argument: unused
 *
 * @return	 0	on success
 */
static int com_codec_name(struct re_printf *pf, void *unused)
{
	int err;

	(void)unused;

	err = re_hprintf(pf, "Codec '%s' used\n",
			audio_codec_get(call_audio(ua_call(menu_uacur()))));

	return err;
}


/**
 * Stop playback of all audio files started with  com_start_play_file
 *
 * @param pf		Print handler for debug output.
 * @param arg		Command argument: unused
 *
 * @return	 0	on success
 */
static int com_stop_play_file(struct re_printf *pf, void *unused)
{
	(void)pf;
	(void)unused;

	/* stop any playback */
	play = mem_deref(play);

	return 0;
}


/**
 * Play the audio file given
 *
 * @param pf		Print handler for debug output.
 * @param arg		Command argument: filename of audio

 * @return	 0	on success
 */
static int com_start_play_file(struct re_printf *pf, void *arg)
{
	struct cmd_arg *carg = arg;
	const char *filename = carg->prm;
	struct config *cfg;
	int err;

	cfg = conf_config();
	err = re_hprintf(pf, "playing audio file \"%s\" ..\n", filename);
	if (err)
		return err;

	err = play_file(&play, baresip_player(), filename, 0,
			cfg->audio.alert_mod, cfg->audio.alert_dev);
	if (err) {
		warning("commend commands: play_file(%s) failed (%m)\n",
			filename, err);
		return err;
	}

	return err;
}


/**
 * Set a current call by its id
 *
 * @param pf		Print handler for debug output. unused
 * @param arg		Command argument: line ID

 * @return	 0	on success
 *			-1	Parameter out of range
 *			-2	No element found
 */
static int com_set_line_by_id(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	const char *p_call_id_arg = NULL;
	const char *p_call_id = NULL;
	struct le *p_ua_le = NULL;
	struct ua *p_ua = NULL;
	struct le *p_call_le = NULL;
	struct call *p_call = NULL;
	struct ua *p_line_ua = NULL;
	struct call *p_line_call = NULL;

	(void)pf;

	if (!carg)
		return -1;

	p_call_id_arg = carg->prm;

	if (!p_call_id_arg || !strlen(p_call_id_arg))
		return -1;

	for (p_ua_le = list_head(uag_list()); p_ua_le;
			p_ua_le = p_ua_le->next) {

		p_ua = p_ua_le->data;

		for (p_call_le = list_head(ua_calls(p_ua)); p_call_le;
				p_call_le = p_call_le->next) {

			p_call = p_call_le->data;
			p_call_id = call_id(p_call);

			if (p_call_id && !strcmp(p_call_id, p_call_id_arg)) {
				p_line_ua = p_ua;
				p_line_call = p_call;
				break;
			}

			/* Caution: Don't change linked lists within
			 * list-for-loop! Otherwise this will crash */
		}
	}

	if (p_line_call) {
		call_set_current(ua_calls(p_line_ua), p_line_call);
		return 0;
	}
	else {
		return -2;
	}
}


/**
 * Find next not established call
 *
 * @param p_ua_ne	Pointer to user agent with not established call
 * @param p_call_ne	Pointer to not established call

 * @return
 */
static void find_not_established_call(struct ua **p_ua_ne,
		struct call **p_call_ne)
{
	struct le *p_ua_le = NULL;
	struct ua *p_ua = NULL;
	struct le *p_call_le = NULL;
	struct call *p_call = NULL;
	const char *p_state = NULL;

	*p_ua_ne = NULL;
	*p_call_ne = NULL;

	for (p_ua_le = list_head(uag_list()); p_ua_le;
			p_ua_le = p_ua_le->next) {

		p_ua = p_ua_le->data;

		for (p_call_le = list_head(ua_calls(p_ua)); p_call_le;
				p_call_le = p_call_le->next) {

			p_call = p_call_le->data;
			p_state = call_statename(p_call);

			if (p_state && strcmp(p_state, "ESTABLISHED")) {
				*p_ua_ne = p_ua;
				*p_call_ne = p_call;
			}

			/* Caution: Don't change linked lists within
			 * list-for-loop! Otherwise this will crash */
		}
	}
}


/**
 * Hangup all not established calls for all user agents
 *
 * @param pf		Print handler for debug output. unused
 * @param unused	unused parameter

 * @return	 0	allways successful
 */
static int com_hangup_not_established(struct re_printf *pf, void *unused)
{
	struct ua *p_ua = NULL;
	struct call *p_call = NULL;

	(void)pf;
	(void)unused;

	while (find_not_established_call(&p_ua, &p_call), p_ua && p_call)
		ua_hangup(p_ua, p_call, 0, NULL);

	return 0;
}


/**
 * Hangup all calls for all user agents
 *
 * @param pf		Print handler for debug output. unused
 * @param unused	unused parameter

 * @return	 0	allways successful
 */
static int com_hangup_all(struct re_printf *pf, void *unused)
{
	struct ua *ua = NULL;
	struct le *le = NULL;

	(void)unused;
	(void)pf;

	for (le = list_head(uag_list()); le; le = le->next) {

		ua = le->data;

		while (ua_call(ua))
			ua_hangup(ua, NULL, 0, NULL);
	}

	return 0;
}


/**
 * Search for proxy server list element in
 * proxy server list.
 * If no number given the current proxy will be set.
 *
 * @param carg
 * @return	 0	on success
 *			-1	Parameter out of range
 *			-2	No element found
 */
static int search_ua(struct le **lep, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua;
	struct le *le = NULL;
	unsigned long value = LONG_MIN;
	int err = 0;

	if (str_isset(carg->prm)) {
		value = strtoul(carg->prm, NULL, 10);
		if (value >= 1 && value <= list_count(uag_list())) {
			/*Iterate to selected server */
			for (le = list_head(uag_list()); value > 1;
					le = le->next) {
				--value;
			}
		}
		else {
			err = -1;
		}
	}
	else {
		ua = menu_uacur();
		/*search for current entry */
		for (le = list_head(uag_list());
				le->data != ua; le = le->next) {
		}
	}

	if (le && lep)
		*lep = le;
	else
		err = -2;

	return err;
}


/**
 * Get registration state for server number given by arg,
 * if arg is empty the current server will be used.
 *
 * @param pf	Print handler for debug output
 * @param arg	Command argument
 *
 * @return	 0	if success
 *			-1	Parameter out of range
 */
static int com_ua_is_register(struct re_printf *pf, void *arg)
{
	struct le *le = NULL;
	struct ua *ua;
	int err = 0;

	err = search_ua(&le, arg);

	if (!err) {
		ua = le->data;

		/*Print registration state */
		err = re_hprintf(pf, "Server %s is %sregistered\n",
				account_aor(ua_account(ua)),
				!ua_isregistered(ua) ? "not " : "");
	}

	if (err) {
		warning("commend commands: register server failed: %m\n", err);
	}
	else {
		debug("commend commands: register server successful");
	}

	return err;
}


/**
 * Start registration for server number given by arg,
 * if arg is empty the current server will be used.
 *
 * @param pf	Print handler for debug output
 * @param arg	Command argument
 *
 * @return	 0	if success
 *			-1	Parameter out of range
 */
static int com_ua_register(struct re_printf *pf, void *arg)
{
	struct le *le = NULL;
	struct ua *ua;
	int err = 0;

	err = search_ua(&le, arg);

	if (!err) {
		ua = le->data;

		/*Register selected server */
		if (!ua_isregistered(ua)) {
			(void)ua_register(ua);
			err = re_hprintf(pf, "Register %s\n",
					account_aor(ua_account(ua)));
		}
	}

	if (err) {
		warning("commend commands: register server failed: %m\n", err);
	}
	else {
		debug("commend commands: register server successful");
	}

	return err;
}


/**
 * Delete server number given by arg, if arg is empty
 * the current server will be deleted.
 *
 * @param pf	Print handler for debug output
 * @param arg	Command argument
 *
 * @return	 0	if success
 *			-1	unable to delete last element
 *			-2	Parameter out of range
 */
static int com_ua_delete(struct re_printf *pf, void *arg)
{
	struct le *le = NULL;
	struct ua *ua;
	int err = 0;

	if (list_count(uag_list()) == 1) {
		re_hprintf(pf, "Unable to delete last element\n");
		return -1;
	}

	err = search_ua(&le, arg);

	if (!err) {
		ua = le->data;

		/*Delete selected server */
		if (ua_isregistered(ua)) {
			err = re_hprintf(pf, "Unregister %s\n",
					account_aor(ua_account(ua)));
		}
		err = re_hprintf(pf, "Delete %s\n",
				account_aor(ua_account(ua)));
		mem_deref(ua);	/*ua_destructor() deletes element from list */
	}

	if (err) {
		warning("commend commands: delete server failed: %m\n", err);
	}
	else {
		debug("commend commands: delete server successful");
	}

	return err;
}


/**
 * Remove user agent time entry which is destroyed in the near future
 * from user agent time list
 *
 * @param arg		Pointer to user agent time entry
 *
 * @return		None
 */
static void ua_time_destructor(void *arg)
{
	struct ua_time *p_time = arg;
	list_unlink(&p_time->le);
}


/**
 * Update user agent time entry
 *
 * @param p_ua		User Agent
 * @param ev		Event
 *
 * @return		None
 */
static void update_ua_reg_time_entry(struct ua *p_ua)
{
	struct le *time_elem;

	uint64_t reg_time = tmr_jiffies();
	struct ua_time *p_time;

	for (time_elem = list_head(&ua_reg_times); time_elem;
			time_elem = time_elem->next) {
		p_time = time_elem->data;

		if (p_time && p_time->p_ua == p_ua) {
			p_time->reg_time = reg_time;

			return;
		}
	}

	/* create entry if necessary */
	p_time = mem_zalloc(sizeof(*p_time), ua_time_destructor);

	if (p_time) {
		p_time->p_ua = p_ua;
		p_time->reg_time = reg_time;

		list_append(&ua_reg_times, &p_time->le, p_time);
	}
}


/**
 * Remove user agent time entry from list
 *
 * @param p_ua		User Agent
 * @param ev		Event
 *
 * @return		None
 */
static void remove_ua_reg_time_entry(struct ua *p_ua)
{
	struct le *time_elem;

	for (time_elem = list_head(&ua_reg_times); time_elem;
			time_elem = time_elem->next) {
		struct ua_time *p_time = time_elem->data;

		if (p_time && p_time->p_ua == p_ua) {
			mem_deref(p_time);

			break;
		}
	}
}


/**
 * Redirect events to internal functions
 *
 * @param ua		User Agent
 * @param ev		Event
 * @param call		Not used
 * @param prm		Not used
 * @param arg		Not used
 *
 * @return		None
 */
static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	(void)prm;
	(void)arg;

	switch (ev) {

	case UA_EVENT_REGISTER_OK:
		update_ua_reg_time_entry(ua);
		break;

	case UA_EVENT_REGISTER_FAIL:
	case UA_EVENT_REGISTERING:
	case UA_EVENT_UNREGISTERING:
		remove_ua_reg_time_entry(ua);
		break;

	case UA_EVENT_CALL_ESTABLISHED:
		{
			struct audio *audio = call_audio(call);
			audio_mute(audio, mute);
		}
		break;

	default:
		break;
	}
}


/**
 * Print the current status of proxy servers
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_reginfo(struct re_printf *pf, void *unused)
{
	struct list *p_uag = uag_list();
	struct le *ua_elem;
	int res = 0;

	(void)unused;

	res |= re_hprintf(pf, "--- Commend UAs: %u ---\n", list_count(p_uag));

	for (ua_elem = list_head(p_uag); ua_elem; ua_elem = ua_elem->next) {
		struct ua *p_ua = ua_elem->data;
		struct le *time_elem;
		uint32_t reg_duration = 0;
		enum reg_status_t reg_status = STATUS_NOT_REGISTERED;
		bool isreachable;
		bool isregistered;
		int32_t pexpire;

		for (time_elem = list_head(&ua_reg_times); time_elem;
				time_elem = time_elem->next) {
			struct ua_time *p_time = time_elem->data;

			if (p_time && p_time->p_ua == p_ua) {
				reg_duration = (uint32_t)((tmr_jiffies() -
						p_time->reg_time) / 1000);
				break;
			}
		}

		isreachable = ua_isregistered(p_ua) &&
				   reg_duration == 0 && ua_regint(p_ua) == 0;
		isregistered  = ua_isregistered(p_ua) &&
				     ua_regint(p_ua) > 0;
		pexpire = 0;
		/* server expire time is only valid for registered servers */
		if (ua_regint(p_ua)!=0)
			pexpire = account_regint(ua_account(p_ua));

		if (isregistered)
			reg_status = STATUS_REGISTERED;
		else if (isreachable)
			reg_status = STATUS_REACHABLE;
		else if (ua_isdisabled(p_ua))
			reg_status = STATUS_DISABLED;

		res |= re_hprintf(pf, "%s %s %u %u %u\n",
					p_ua == menu_uacur() ? ">" : " ",
					account_aor(ua_account(p_ua)),
					reg_status,
					pexpire,
					reg_duration);
	}

	return res;
}


/**
 * Set mute on or off, if no parameter given show current state
 *
 * @param pf		Print handler for debug output
 * @param arg		Command argument
 *
 * @return	 0		if success
 *			-1		no argument given
 */
static int com_mic_mute(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct audio *audio = call_audio(ua_call(menu_uacur()));
	int err = 0;

	(void)pf;

	if (str_isset(carg->prm)) {
		mute = !strcmp(carg->prm, "on");
		audio_mute(audio, mute);
	}
	else {
		(void)re_hprintf(pf, "call %smuted\n", mute ? "" : "un-");
	}

	if (err) {
		warning("commend commands: setting microphone mute failed:"
				" %m\n", err);
	}
	else {
		debug("commend commands: microphone mute is %d", mute);
	}

	return 0;
}


/**
 * Print the current used memory returned from Linux kernel
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_get_memory(struct re_printf *pf, void *unused)
{
	long memUsage;
	FILE *f;
	(void)unused;

	memUsage = 0;
	f = fopen("/proc/self/status", "r");

	if (f) {
		char tmp_buffer[512];
		char *p;
		fread(tmp_buffer, 512, 1, f);
		p = strstr(tmp_buffer, "Mem:");
		if (p) {
			p += 4; /* strlen(Mem;) */
			memUsage = strtol(p, NULL, 10);
		}
	}

	fclose(f);
	return re_hprintf(pf, "Mem usage: %ld", memUsage);
}


/**
 * Initialize SIP log structure
 */
static void com_sip_log_init(void)
{
	int i;

	sip_log.idx = 0;
	for (i = 0; i < LOG_SIZE; ++i)
		sip_log.entries[i].p_buffer = NULL;

	enable_sip_log(&sip_log);
}


/**
 * Disable SIP log and cleanup log entries
 */
static void com_sip_log_disable_and_cleanup(void)
{
	int i;

	disable_sip_log();

	sip_log.idx = 0;
	for (i = 0; i < LOG_SIZE; ++i) {
		if (sip_log.entries[i].p_buffer) {
			mem_deref(sip_log.entries[i].p_buffer);
			sip_log.entries[i].p_buffer = NULL;
		}
	}
}


/**
 * Print SIP log
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_sip_trace(struct re_printf *pf, void *unused)
{
	int i, read_idx, res = 0;

	(void)unused;

	read_idx = sip_log.idx - 1;

	for (i = 0; i < LOG_SIZE; ++i) {

		read_idx &= LOG_IDX_MASK;

		if (sip_log.entries[read_idx].p_buffer) {
			struct timeval *p_ts =
				&sip_log.entries[read_idx].timestamp;

			res |= re_hprintf(pf, "Timestamp: %d.%06d\n",
					p_ts->tv_sec,
					p_ts->tv_usec);
			if (sip_log.entries[read_idx].direction ==
					LOG_DIR_SEND)
				res |= re_hprintf(pf, "--->>>\n");
			else
				res |= re_hprintf(pf, "<<<---\n");
			res |= re_hprintf(pf, "\n%s\n__MSG_LINE__\n\n",
					sip_log.entries[read_idx].p_buffer);
		}

		--read_idx;
	}

	return res;
}


/**
 * Clear SIP log
 *
 * @param pf		Print handler for debug output
 * @param unused	unused parameter
 *
 * @return	0 if success, otherwise errorcode
 */
static int com_sip_trace_clear(struct re_printf *pf, void *unused)
{
	(void)unused;
	(void)pf;

	com_sip_log_disable_and_cleanup();
	enable_sip_log(&sip_log);

	return 0;
}

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
		err = ua_print_calls(pf, ua, com_call_info);
		if (err)
			return err;
	}

	return 0;
}


static const struct cmd cmdv[] = {

{"com_listcalls", 0, 0, "List active calls Commend format", com_print_calls},
{"com_hangup_all", 0, 0, "Hangup all calls", com_hangup_all},
{"com_hangup_not_est", 0, 0, "Hangup all calls which are not established",
	com_hangup_not_established},
{"com_set_line_by_id", 0, 0, "Set line by ID", com_set_line_by_id},
{"com_memory", 0, 0, "Show used process memory", com_get_memory},
{"com_mic_mute", 0, CMD_PRM, "Set microphone mute on/off", com_mic_mute},
{"com_sip_trace", 0, 0, "Show SIP trace", com_sip_trace},
{"com_sip_trace_clear", 0, 0, "Clear SIP trace", com_sip_trace_clear},
{"com_reginfo", 0, 0, "Proxy server registration details", com_reginfo},
{"com_ua_del", 0, CMD_PRM, "Delete a proxy server", com_ua_delete},
{"com_ua_reg", 0, CMD_PRM, "Register a proxy server", com_ua_register},
{"com_ua_isreg", 0, CMD_PRM, "Is proxy server registered", com_ua_is_register},
{"com_play", 0, CMD_PRM, "Start audio file playback", com_start_play_file},
{"com_stop", 0, 0, "Stop audio file playback", com_stop_play_file},
{"com_codec_cur", 0, 0, "Codec name of current call", com_codec_name},
{"com_sip_cert", 0, 0, "Is SIP certificate set", com_sip_certificate},
{"com_sip_ca", 0, 0, "Is SIP CA set", com_sip_ca},

};


int commend_menu_register(void)
{
	int err;

	com_sip_log_init();

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	return err;
}


void commend_menu_unregister(void)
{
	cmd_unregister(baresip_commands(), cmdv);

	com_sip_log_disable_and_cleanup();
	uag_event_unregister(ua_event_handler);
	list_flush(&ua_reg_times);
}
