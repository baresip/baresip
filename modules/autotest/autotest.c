/**
 * @file autotest.c Autotest module
 *
 * Supports automatic repeated dialing and hangup via timers. The commands for
 * dialing and hangup can be specified by means of the registered commands.
 *
 * E.g.:
 * On host A:
 * /autodial dial 10.1.0.215
 *
 * On host B with IP 10.1.0.215:
 * /autohangupdelay 2000
 *
 * Copyright (C) 2021 Commend.com - c.spielberger@commend.com
 */

#include <string.h>
#include <stdlib.h>
#include <re.h>
#include <baresip.h>

struct autotest {
	struct mbuf *mbdial;          /**< Dial command                      */
	struct mbuf *mbhangup;        /**< Hangup command                    */
	uint64_t dt_dial;             /**< Delay before auto dial            */
	uint64_t dt_hangup;           /**< Delay before hangup               */

	struct tmr tmr_dial;          /**< Timer invokes dial command        */
	struct tmr tmr_hangup;        /**< Timer invokes hangup command      */
	int cnt_dial;                 /**< Dial counter                      */
	int cnt_hangup;               /**< Hangup counter                    */
};


static struct autotest d;


static int response_print(const char *p, size_t size, void *arg)
{
	struct pl pl;
	(void) arg;
	pl.p = p;
	pl.l = size;

	info("%r", &pl);
	return 0;
}


static void hangup(void *arg)
{
	struct re_printf pf = {response_print, NULL};
	struct pl pl;
	int err;
	(void) arg;

	if (!d.mbhangup)
		return;

	pl_set_mbuf(&pl, d.mbhangup);
	info("autotest: hangup (%r)\n", &pl);
	err = cmd_process_long(baresip_commands(), pl.p, pl.l, &pf, NULL);

	if (err) {
		warning("autotest: hangup error (%m)\n", err);
		return;
	}

	d.cnt_hangup++;
}


static void dial(void *arg)
{
	struct re_printf pf = {response_print, NULL};
	struct pl pl;
	int err;
	(void) arg;

	if (!d.mbdial)
		return;

	info("autotest: dial (%r)\n", &pl);
	pl_set_mbuf(&pl, d.mbdial);
	err = cmd_process_long(baresip_commands(), pl.p, pl.l, &pf, NULL);

	if (err) {
		warning("autotest: dial error (%m)\n", err);
		return;
	}

	d.cnt_dial++;
}


static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct account *acc = ua_account(ua);
	(void) arg;

	info("autotest: [ ua=%s call=%s ] event: %s (%s)\n",
	      account_aor(acc), call_id(call), uag_event_str(ev), prm);

	switch (ev) {

	case UA_EVENT_CALL_INCOMING:
	case UA_EVENT_CALL_RINGING:
	case UA_EVENT_CALL_PROGRESS:
	case UA_EVENT_CALL_ANSWERED:
	case UA_EVENT_CALL_ESTABLISHED:
	case UA_EVENT_CALL_REMOTE_SDP:
	case UA_EVENT_CALL_TRANSFER:
	case UA_EVENT_CALL_TRANSFER_FAILED:
		if (d.dt_hangup)
			tmr_start(&d.tmr_hangup, d.dt_hangup, hangup, NULL);
		break;

	case UA_EVENT_CALL_CLOSED:
		if (d.dt_dial)
			tmr_start(&d.tmr_dial, d.dt_dial, dial, NULL);
		break;

	default:
		break;
	}

}


static int cmd_autodial(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	int err;

	if (!str_isset(carg->prm)) {
		re_hprintf(pf, "Usage:\n  autodial <cmd>\n");
		return EINVAL;
	}

	d.mbdial = mem_deref(d.mbdial);
	d.mbdial = mbuf_alloc(256);
	if (!d.dt_dial)
		d.dt_dial = 5 * 1000;

	err = mbuf_printf(d.mbdial, carg->prm);
	if (err)
		return err;

	mbuf_set_pos(d.mbdial, 0);
	tmr_start(&d.tmr_dial, d.dt_dial, dial, NULL);

	re_hprintf(pf, "autotest: dial command set to \"%s\", "
			"delay is %lu ms\n", carg->prm, d.dt_dial);
	return 0;
}


static int cmd_autohangup(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl = PL_INIT;
	int err;

	if (!str_isset(carg->prm)) {
		re_hprintf(pf, "Usage:\n  autohangup <cmd>\n");
		return EINVAL;
	}

	d.mbhangup = mem_deref(d.mbhangup);
	d.mbhangup = mbuf_alloc(256);
	if (!d.dt_hangup)
		d.dt_hangup = 5 * 1000;

	err = mbuf_printf(d.mbhangup, carg->prm);
	mbuf_set_pos(d.mbhangup, 0);

	pl_set_mbuf(&pl, d.mbhangup);
	re_hprintf(pf, "autotest: hangup command set to \"%r\", "
			"delay is %lu ms\n", &pl, d.dt_hangup);
	return err;
}


static int cmd_autodial_cancel(struct re_printf *pf, void *arg)
{
	(void) arg;

	d.mbdial = mem_deref(d.mbdial);
	tmr_cancel(&d.tmr_dial);

	re_hprintf(pf, "autotest: auto dial canceled\n");
	return 0;
}


static int cmd_autohangup_cancel(struct re_printf *pf, void *arg)
{
	(void) arg;

	d.mbhangup = mem_deref(d.mbhangup);
	tmr_cancel(&d.tmr_hangup);

	re_hprintf(pf, "autotest: auto hangup canceled\n");
	return 0;
}


static int cmd_dial_delay(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl = PL_INIT;

	d.dt_dial = atoi(carg->prm);

	pl_set_mbuf(&pl, d.mbdial);
	re_hprintf(pf, "autotest: delay for dial command \"%r\" set to "
			"%lu ms\n", &pl, d.dt_dial);
	return 0;
}


static int cmd_hangup_delay(struct re_printf *pf, void *arg)
{
	const struct cmd_arg *carg = arg;
	struct pl pl = PL_INIT;
	int err;

	d.dt_hangup = atoi(carg->prm);
	if (!d.mbhangup) {
		d.mbhangup = mbuf_alloc(256);
		err = mbuf_printf(d.mbhangup, "hangup");
		if (err)
			return err;

		mbuf_set_pos(d.mbhangup, 0);
	}

	pl_set_mbuf(&pl, d.mbhangup);
	re_hprintf(pf, "autotest: delay for hangup command \"%r\" set to %lu "
			"ms\n", &pl, d.dt_hangup);

	tmr_start(&d.tmr_hangup, d.dt_hangup, hangup, NULL);
	return 0;
}


static int cmd_stat(struct re_printf *pf, void *arg)
{
	struct pl pl;
	(void) arg;

	re_hprintf(pf, "autotest:\n");
	pl.l = 0;
	pl_set_mbuf(&pl, d.mbdial);
	re_hprintf(pf, "  dial command   : %r\n", &pl);
	pl.l = 0;
	pl_set_mbuf(&pl, d.mbhangup);
	re_hprintf(pf, "  hangup command : %r\n", &pl);
	re_hprintf(pf, "  dial delay     : %lu (expire %lu ms)\n",
			d.dt_dial, tmr_get_expire(&d.tmr_dial));
	re_hprintf(pf, "  dial counter   : %u\n", d.cnt_dial);
	re_hprintf(pf, "  hangup delay   : %lu (expire %lu ms)\n",
			d.dt_hangup, tmr_get_expire(&d.tmr_hangup));
	re_hprintf(pf, "  hangup counter : %u\n", d.cnt_hangup);
	return 0;
}


static const struct cmd cmdv[] = {

{"autodial", 0, CMD_PRM, "Set auto dial command", cmd_autodial               },
{"autohangup", 0, CMD_PRM, "Set auto hangup command", cmd_autohangup         },
{"autodialdelay", 0, CMD_PRM, "Set delay before auto dial [ms]",
	cmd_dial_delay   },
{"autohangupdelay", 0, CMD_PRM, "Set delay before hangup [ms]",
	cmd_hangup_delay },
{"autodialcancel", 0, 0, "Cancel auto dial", cmd_autodial_cancel },
{"autohangupcancel", 0, 0, "Cancel auto hangup", cmd_autohangup_cancel },
{"autostat", 0, 0, "Print autotest status",  cmd_stat                        },
};


static int module_init(void)
{
	int err;
	info("autotest: module init\n");

	memset(&d, 0, sizeof(d));
	err = uag_event_register(ua_event_handler, NULL);
	if (err)
		return err;

	err = cmd_register(baresip_commands(), cmdv, ARRAY_SIZE(cmdv));
	return err;
}


static int module_close(void)
{
	info("autotest: module closed\n");

	tmr_cancel(&d.tmr_hangup);
	tmr_cancel(&d.tmr_dial);
	cmd_unregister(baresip_commands(), cmdv);
	uag_event_unregister(ua_event_handler);
	mem_deref(d.mbdial);
	mem_deref(d.mbhangup);
	return 0;
}


const struct mod_export DECL_EXPORTS(autotest) = {
	"autotest",
	"application",
	module_init,
	module_close
};
