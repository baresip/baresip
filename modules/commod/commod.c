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


static const struct cmd cmdv[] = {

{"com_listcalls", 0, 0, "List active calls Commend format", com_print_calls},

};


static int module_init(void)
{
	int err;

	err  = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));

	return err;
}


static int module_close(void)
{

	cmd_unregister(baresip_commands(), cmdv);

	return 0;
}


const struct mod_export DECL_EXPORTS(commod) = {
	"commod",
	"application",
	module_init,
	module_close
};
