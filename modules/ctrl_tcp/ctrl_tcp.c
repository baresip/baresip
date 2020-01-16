/**
 * @file ctrl_tcp.c  TCP control interface using JSON payload
 *
 * Copyright (C) 2018 46 Labs LLC
 */

#include <re.h>
#include <baresip.h>

#include "tcp_netstring.h"


/**
 * @defgroup ctrl_tcp ctrl_tcp
 *
 * Communication channel to control and monitor Baresip via JSON messages.
 *
 * It receives commands to be executed, sends back command responses and
 * notifies about events.
 *
 * Command message parameters:
 *
 * - command : Command to be executed.
 * - params  : Command parameters.
 * - token   : Optional. Included in the response if present.
 *
 * Command message example:
 *
 \verbatim
 {
  "command" : "dial",
  "params"  : "sip:alice@atlanta.com",
  "token"   : "qwerasdf"
 }
 \endverbatim
 *
 *
 * Response message parameters:
 *
 * - response : true. Identifies the message type.
 * - ok:      : true/false. Indicates whether the command execution succeeded.
 * - data     : Baresip response to the related command execution.
 * - token    : Present if it was included in the related command request.
 *
 * Response message example:
 *
 \verbatim
 {
  "response" : true,
  "ok"       : true,
  "data"     : "",
  "token"    : "qwerasdf"
 }
 \endverbatim
 *
 *
 * Event message parameters:
 *
 * - event     : true. Identifies the message type.
 * - class     : Event class.
 * - type      : Event ID.
 * - param     : Specific event information.
 *
 * Apart from the above, events may contain aditional parameters.
 *
 * Event message example:
 *
 \verbatim
 {
  "event"      : "true",
  "class"      : "call",
  "type"       : "CALL_CLOSED",
  "param"      : "Connection reset by peer",
  "accountaor" : "sip:alice@atlanta.com",
  "direction"  : "incoming",
  "peeruri"    : "sip:bob@biloxy.com",
  "id"         : "73a12546589651f8"
 }
 \endverbatim
 *
 *
 * Sample config:
 *
 \verbatim
  ctrl_tcp_listen     0.0.0.0:4444         # IP-address and port to listen on
 \endverbatim
 */


enum {CTRL_PORT = 4444};

struct ctrl_st {
	struct tcp_sock *ts;
	struct tcp_conn *tc;
	struct netstring *ns;
};

static struct ctrl_st *ctrl = NULL;  /* allow only one instance */

static int print_handler(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;

	return mbuf_write_mem(mb, (uint8_t *)p, size);
}


static int encode_response(int cmd_error, struct mbuf *resp, const char *token)
{
	struct re_printf pf = {print_handler, resp};
	struct odict *od = NULL;
	char *buf = NULL;
	char m[256];
	int err;

	/* Empty response. */
	if (resp->pos == NETSTRING_HEADER_SIZE)
	{
		buf = mem_alloc(1, NULL);
		buf[0] = '\0';
	}
	else
	{
		resp->pos = NETSTRING_HEADER_SIZE;
		err = mbuf_strdup(resp, &buf,
			resp->end - NETSTRING_HEADER_SIZE);
		if (err)
			return err;
	}

	err = odict_alloc(&od, 8);
	if (err)
		return err;

	err |= odict_entry_add(od, "response", ODICT_BOOL, true);
	err |= odict_entry_add(od, "ok", ODICT_BOOL, (bool)!cmd_error);

	if (cmd_error && str_len(buf) == 0)
		err |= odict_entry_add(od, "data", ODICT_STRING,
			str_error(cmd_error, m, sizeof(m)));
	else
		err |= odict_entry_add(od, "data", ODICT_STRING, buf);

	if (token)
		err |= odict_entry_add(od, "token", ODICT_STRING, token);

	if (err)
		goto out;

	mbuf_reset(resp);
	mbuf_init(resp);
	resp->pos = NETSTRING_HEADER_SIZE;

	err = json_encode_odict(&pf, od);
	if (err)
		warning("ctrl_tcp: failed to encode response JSON (%m)\n",
			err);

 out:
	mem_deref(buf);
	mem_deref(od);

	return err;
}


static bool command_handler(struct mbuf *mb, void *arg)
{
	struct ctrl_st *st = arg;
	struct mbuf *resp = mbuf_alloc(2048);
	struct re_printf pf = {print_handler, resp};
	struct odict *od = NULL;
	const struct odict_entry *oe_cmd, *oe_prm, *oe_tok;
	char buf[1024];
	int err;

	err = json_decode_odict(&od, 32, (const char*)mb->buf, mb->end, 16);
	if (err) {
		warning("ctrl_tcp: failed to decode JSON (%m)\n", err);
		goto out;
	}

	oe_cmd = odict_lookup(od, "command");
	oe_prm = odict_lookup(od, "params");
	oe_tok = odict_lookup(od, "token");
	if (!oe_cmd) {
		warning("ctrl_tcp: missing json entries\n");
		goto out;
	}

	debug("ctrl_tcp: handle_command:  cmd='%s', params:'%s', token='%s'\n",
	      oe_cmd->u.str,
	      oe_prm ? oe_prm->u.str : "",
	      oe_tok ? oe_tok->u.str : "");

	re_snprintf(buf, sizeof(buf), "%s%s%s",
		    oe_cmd->u.str,
		    oe_prm ? " " : "",
		    oe_prm ? oe_prm->u.str : "");

	resp->pos = NETSTRING_HEADER_SIZE;

	/* Relay message to long commands */
	err = cmd_process_long(baresip_commands(),
			       buf,
			       str_len(buf),
			       &pf, NULL);
	if (err) {
		warning("ctrl_tcp: error processing command (%m)\n", err);
	}

	err = encode_response(err, resp, oe_tok ? oe_tok->u.str : NULL);
	if (err) {
		warning("ctrl_tcp: failed to encode response (%m)\n", err);
		goto out;
	}

	resp->pos = NETSTRING_HEADER_SIZE;
	err = tcp_send(st->tc, resp);
	if (err) {
		warning("ctrl_tcp: failed to send the message (%m)\n", err);
	}

 out:
	mem_deref(resp);
	mem_deref(od);

	return true;  /* always handled */
}


static void tcp_close_handler(int err, void *arg)
{
	struct ctrl_st *st = arg;

	(void)err;

	st->tc = mem_deref(st->tc);
}


static void tcp_conn_handler(const struct sa *peer, void *arg)
{
	struct ctrl_st *st = arg;

	(void)peer;

	/* only one connection allowed */
	st->tc = mem_deref(st->tc);
	st->ns = mem_deref(st->ns);

	(void)tcp_accept(&st->tc, st->ts, NULL, NULL, tcp_close_handler, st);
	(void)netstring_insert(&st->ns, st->tc, 0, command_handler, st);
}


/*
 * Relay UA events
 */
static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct ctrl_st *st = arg;
	struct mbuf *buf = mbuf_alloc(1024);
	struct re_printf pf = {print_handler, buf};
	struct odict *od = NULL;
	int err;

	buf->pos = NETSTRING_HEADER_SIZE;

	err = odict_alloc(&od, 8);
	if (err)
		return;

	err = odict_entry_add(od, "event", ODICT_BOOL, true);
	err |= event_encode_dict(od, ua, ev, call, prm);
	if (err) {
		warning("ctrl_tcp: failed to encode event (%m)\n", err);
		goto out;
	}

	err = json_encode_odict(&pf, od);
	if (err) {
		warning("ctrl_tcp: failed to encode json (%m)\n", err);
		goto out;
	}

	if (st->tc) {
		buf->pos = NETSTRING_HEADER_SIZE;
		err = tcp_send(st->tc, buf);
		if (err) {
			warning("ctrl_tcp: failed to send the message (%m)\n",
				err);
		}
	}

 out:
	mem_deref(buf);
	mem_deref(od);
}


static void ctrl_destructor(void *arg)
{
	struct ctrl_st *st = arg;

	mem_deref(st->tc);
	mem_deref(st->ts);
	mem_deref(st->ns);
}


static int ctrl_alloc(struct ctrl_st **stp, const struct sa *laddr)
{
	struct ctrl_st *st;
	int err;

	if (!stp)
		return EINVAL;

	st = mem_zalloc(sizeof(*st), ctrl_destructor);
	if (!st)
		return ENOMEM;

	err = tcp_listen(&st->ts, laddr, tcp_conn_handler, st);
	if (err) {
		warning("ctrl_tcp: failed to listen on TCP %J (%m)\n",
			laddr, err);
		goto out;
	}

	debug("ctrl_tcp: TCP socket listening on %J\n", laddr);

 out:
	if (err)
		mem_deref(st);
	else
		*stp = st;

	return err;
}


static int ctrl_init(void)
{
	struct sa laddr;
	int err;

	if (conf_get_sa(conf_cur(), "ctrl_tcp_listen", &laddr)) {
		sa_set_str(&laddr, "0.0.0.0", CTRL_PORT);
	}

	err = ctrl_alloc(&ctrl, &laddr);
	if (err)
		return err;

	err = uag_event_register(ua_event_handler, ctrl);
	if (err)
		return err;

	return 0;
}


static int ctrl_close(void)
{
	uag_event_unregister(ua_event_handler);
	ctrl = mem_deref(ctrl);

	return 0;
}


const struct mod_export DECL_EXPORTS(ctrl_tcp) = {
	"ctrl_tcp",
	"application",
	ctrl_init,
	ctrl_close
};
