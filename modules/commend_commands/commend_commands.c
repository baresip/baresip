/**
 * @file commend_commands.c  Commend specific commands
 *
 * Copyright (C) 2018 Commend.com
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <re.h>
#include <baresip.h>

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
	STATUS_REGISTERED
};

struct ua_time {
	uint64_t reg_time;
	struct le le;
	struct ua *p_ua;
};

struct sip_log_t sip_log;
static struct list ua_reg_times = LIST_INIT;

/**
 * Set a current call by its id
 *
 * @param pf		Print handler for debug output. unused
 * @param arg		Command argument: line ID

 * @return	 0	on success
 * 			-1	Parameter out of range
 * 			-2	No element found
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

	for (p_ua_le = list_head(uag_list()); p_ua_le; p_ua_le = p_ua_le->next) {

		p_ua = p_ua_le->data;

		for (p_call_le = list_head(ua_calls(p_ua)); p_call_le; p_call_le = p_call_le->next) {

			p_call = p_call_le->data;
			p_call_id = call_id(p_call);

			if (p_call_id && !strcmp(p_call_id, p_call_id_arg)) {
				p_line_ua = p_ua;
				p_line_call = p_call;
				break;
			}

			// Caution: Don't change linked lists within list-for-loop! Otherwise this will crash
		}
	}

	if (p_line_call) {
		call_set_current(ua_calls(p_line_ua), p_line_call);
		return 0;
	} else {
		return -2;
	}
}


/**
 * Find next not established call
 *
 * @param p_ua_ne		Pointer to user agent with not established call
 * @param p_call_ne		Pointer to not established call

 * @return
 */
static void find_not_established_call(struct ua **p_ua_ne, struct call **p_call_ne)
{
	struct le *p_ua_le = NULL;
	struct ua *p_ua = NULL;
	struct le *p_call_le = NULL;
	struct call *p_call = NULL;
	const char *p_state = NULL;

	*p_ua_ne = NULL;
	*p_call_ne = NULL;

	for (p_ua_le = list_head(uag_list()); p_ua_le; p_ua_le = p_ua_le->next) {

		p_ua = p_ua_le->data;

		for (p_call_le = list_head(ua_calls(p_ua)); p_call_le; p_call_le = p_call_le->next) {

			p_call = p_call_le->data;
			p_state = call_statename(p_call);

			if (p_state && strcmp(p_state, "ESTABLISHED")) {
				*p_ua_ne = p_ua;
				*p_call_ne = p_call;
			}

			// Caution: Don't change linked lists within list-for-loop! Otherwise this will crash
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

	while(find_not_established_call(&p_ua, &p_call), p_ua && p_call)
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
 * 			-1	Parameter out of range
 * 			-2	No element found
 */
static int search_ua(struct le **le, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct ua *ua;
	struct le *listElement = NULL;
	long value = LONG_MIN;
	int err = 0;

	if (str_isset(carg->prm)) {
		value = strtol(carg->prm, NULL, 10);
		if(value >= 1 && value <= list_count(uag_list())) {
			//Iterate to selected server
			for (listElement = list_head(uag_list()); value > 1; listElement = listElement->next) {
				value--;
			}
		} else {
			err = -1;
		}
	} else {
		ua = uag_current();
		//search for current entry
		for (listElement = list_head(uag_list()); listElement->data != ua; listElement = listElement->next) {
		}
	}

	if(listElement)
		*le = listElement;
	else
		err = -2;

	return err;
}

/**
 * Set current server to number given by arg,
 * if arg is empty the current server will be used.
 *
 * @param pf	Print handler for debug output
 * @param arg	Command argument
 *
 * @return 	 0 	if success
 *			-1	Parameter out of range
 *			-2	Failed to set current server
 */
static int com_ua_set_current(struct re_printf *pf, void *arg)
{
	struct le *le = NULL;
	struct ua *ua;
	int err = 0;

	err = search_ua(&le, arg);

	if (!err) {
		ua = le->data;

		uag_current_set(ua);

		if(ua == uag_current()) {
			//Print active server
			err = re_hprintf(pf, "Server %s activated\n",
					account_aor(ua_account(ua)));
		} else {
			err = -2;
		}
	}

	if (err) {
		warning("commend commands: set current server failed: %m\n", err);
	} else {
		debug("commend commands: set current server successful");
	}

	return err;
}

/**
 * Get registration state for server number given by arg,
 * if arg is empty the current server will be used.
 *
 * @param pf	Print handler for debug output
 * @param arg	Command argument
 *
 * @return 	 0 	if success
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

		//Print registration state
		err = re_hprintf(pf, "Server %s is %sregistered\n",
				account_aor(ua_account(ua)), !ua_isregistered(ua) ? "not " : "");
	}

	if (err) {
		warning("commend commands: register server failed: %m\n", err);
	} else {
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
 * @return 	 0 	if success
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

		//Register selected server
		if(!ua_isregistered(ua)) {
			(void)ua_register(ua);
			err = re_hprintf(pf, "Register %s\n", account_aor(ua_account(ua)));
		}
	}

	if (err) {
		warning("commend commands: register server failed: %m\n", err);
	} else {
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
 * @return 	 0 	if success
 * 			-1	unable to delete last element
 *			-2	Parameter out of range
 */
static int com_ua_delete(struct re_printf *pf, void *arg)
{
	struct le *le = NULL;
	struct ua *ua;
	int err = 0;

	if(list_count(uag_list()) == 1) {
		re_hprintf(pf, "Unable to delete last element\n");
		return -1;
	}

	err = search_ua(&le, arg);

	if (!err) {
		ua = le->data;
		if (ua == uag_current()) {
			//Move to next ua befor delete
			le = le->next ? le->next : list_head(uag_list());
			uag_current_set(le->data);
		}

		//Delete selected server
		if(ua_isregistered(ua)) {
			(void)ua_unregister(ua);	//Clearly remove all clients
			err = re_hprintf(pf, "Unregister %s\n", account_aor(ua_account(ua)));
		}
		err = re_hprintf(pf, "Delete %s\n", account_aor(ua_account(ua)));
		mem_deref(ua);	//ua_destructor() deletes element from list
	}

	if (err) {
		warning("commend commands: delete server failed: %m\n", err);
	} else {
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

	for (time_elem = list_head(&ua_reg_times); time_elem; time_elem = time_elem->next) {
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

	for (time_elem = list_head(&ua_reg_times); time_elem; time_elem = time_elem->next) {
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
	(void)call;
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

		if (ua_isregistered(p_ua))
			reg_status = STATUS_REGISTERED;
		else
		if (ua_isdisabled(p_ua))
			reg_status = STATUS_DISABLED;

		for (time_elem = list_head(&ua_reg_times); time_elem; time_elem = time_elem->next) {
			struct ua_time *p_time = time_elem->data;

			if (p_time && p_time->p_ua == p_ua) {
				reg_duration = (uint32_t)((tmr_jiffies() - p_time->reg_time) / 1000);

				break;
			}
		}

		res |= re_hprintf(pf, "%s %s %u %u %u\n",
						p_ua == uag_current() ? ">" : " ",
						ua_aor(p_ua),
						reg_status,
						ua_regint(p_ua),
						reg_duration
						);
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
	struct audio *audio = call_audio(ua_call(uag_current()));
	bool mute;
	int err = 0;

	(void)pf;

	if (str_isset(carg->prm)) {
		mute = !strcmp(carg->prm, "on");
		audio_mute(audio, mute);
	} else {
		mute = audio_ismuted(audio);
		(void)re_hprintf(pf, "call %smuted\n", mute ? "" : "un-");
	}

	if (err) {
		warning("commend commands: setting microphone mute failed: %m\n", err);
	} else {
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
	(void)unused;
	long memUsage;
	FILE *status_file;

	memUsage = 0;
	status_file = fopen("/proc/self/status", "r");

	if (status_file){
		char tmp_buffer[512];
		fread(tmp_buffer, 512, 1, status_file);
		char* mem;
		mem = strstr(tmp_buffer, "Mem:");
		if (mem) {
			mem += 4; // strlen(Mem;)
			memUsage = strtol(mem, NULL, 10);
		}
	}

	fclose(status_file);

	return re_hprintf(pf, "Mem usage: %ld", memUsage);
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
static int com_print_calls(struct re_printf *pf, void *unused)
{
	(void)unused;
	return ua_print_calls(pf, uag_current(), com_call_info);
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
			struct timeval *p_ts = &sip_log.entries[read_idx].timestamp;

			res |= re_hprintf(pf, "Timestamp: %d.%06d\n", p_ts->tv_sec, p_ts->tv_usec);
			if (sip_log.entries[read_idx].direction == LOG_DIR_SEND)
				res |= re_hprintf(pf, "--->>>\n");
			else
				res |= re_hprintf(pf, "<<<---\n");
			res |= re_hprintf(pf, "\n%s\n__MSG_LINE__\n\n", sip_log.entries[read_idx].p_buffer);
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

static const struct cmd cmdv[] = {

{"com_listcalls",	0,	0,		"List active calls Commend format",	com_print_calls	},
{"com_hangup_all",	0,	0,		"Hangup all calls",					com_hangup_all	},
{"com_hangup_not_est",	0,	0,		"Hangup all calls which are not established",		com_hangup_not_established	},
{"com_set_line_by_id",	0,	0,		"Set line by ID",					com_set_line_by_id	},
{"com_memory"	,	0,	0,		"Show used process memory",			com_get_memory	},
{"com_mic_mute"	,	0,	CMD_PRM,"Set microphone mute on/off",		com_mic_mute	},
{"com_sip_trace",	0,	0,		"Show SIP trace",			com_sip_trace	},
{"com_sip_trace_clear",	0,	0,		"Clear SIP trace",			com_sip_trace_clear	},
{"com_reginfo",		0,	0,		"Proxy server registration details",com_reginfo	},
{"com_ua_del",		0,	CMD_PRM,"Delete a proxy server",			com_ua_delete	},
{"com_ua_reg",		0,	CMD_PRM,"Register a proxy server",			com_ua_register	},
{"com_ua_isreg",	0,	CMD_PRM,"Is proxy server registered",		com_ua_is_register	},
{"com_ua_set_cur",	0,	CMD_PRM,"Set proxy server to use",			com_ua_set_current	},

};

static int module_init(void)
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


static int module_close(void)
{
	cmd_unregister(baresip_commands(), cmdv);

	com_sip_log_disable_and_cleanup();

	return 0;
}


const struct mod_export DECL_EXPORTS(commend_commands) = {
	"commend_commands",
	"application",
	module_init,
	module_close
};
