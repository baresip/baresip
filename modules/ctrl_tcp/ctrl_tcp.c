/**
 * @file ctrl_tcp.c  TCP control interface using JSON payload
 *
 * Copyright (C) 2017 José Luis Millán <jmillan@aliax.net>
 */
#include <re.h>
#include <baresip.h>


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
 * - type     : "response". Identifies the message type.
 * - response : Baresip response to the related command execution.
 * - token    : Present if it was included in the related command request.
 *
 * Response message example:
 *
 \verbatim
 {
  "type"     : "response",
  "response" : "",
  "token"    : "qwerasdf"
 }
 \endverbatim
 *
 *
 * Event message parameters:
 *
 * - type      : "event". Identifies the message type.
 * - class     : Event class.
 * - event     : Event ID.
 * - param     : Specific event information.
 *
 * Apart from the above, events may contain aditional parameters.
 *
 * Event message example:
 *
 \verbatim
 {
  "type"       : "event",
  "class"      : "call",
  "event"      : "CALL_CLOSED",
  "param"      : "Connection reset by peer",
  "account"    : "sip:alice@atlanta.com",
  "direction"  : "incoming",
  "peer"       : "sip:bob@biloxy.com",
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
};


static struct ctrl_st *ctrl = NULL;  /* allow only one instance */


static void ctrl_destructor(void *arg)
{
	struct ctrl_st *st = arg;

	mem_deref(st->tc);
	mem_deref(st->ts);
}


static int print_handler(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;

	return mbuf_write_mem(mb, (uint8_t *)p, size);
}


static int encode_response(struct mbuf *resp, const char *token)
{
	struct re_printf pf = {print_handler, resp};
	struct odict *od = NULL;
	char *buf = NULL;
	int err;

	err = odict_alloc(&od, 8);
	if (err)
		return err;

	resp->pos = 0;
	err = mbuf_strdup(resp, &buf, resp->end);
	if (err)
		return err;

	mbuf_reset(resp);
	mbuf_init(resp);

	err |= odict_entry_add(od, "type", ODICT_STRING, "response");
	err |= odict_entry_add(od, "response", ODICT_STRING, buf);

	if (token)
		err |= odict_entry_add(od, "token", ODICT_STRING, token);

	if (err)
		goto out;

	err = json_encode_odict(&pf, od);
	if (err)
		warning("ctrl_tcp: failed to encode response json (%m)\n", err);

 out:
	mem_deref(buf);
	mem_deref(od);

	return err;
}


static void tcp_recv_handler(struct mbuf *mb, void *arg)
{
	struct ctrl_st *st = arg;
	struct mbuf *resp = mbuf_alloc(1024);
	struct re_printf pf = {print_handler, resp};
	struct odict *od = NULL;
	const struct odict_entry *oe_cmd, *oe_prm, *oe_tok;
	char buf[256];
	int err;

	err = json_decode_odict(&od, 32, (const char*)mb->buf, mb->end, 16);
	if (err) {
		warning("ctrl_tcp: failed to decode JSON with %zu bytes (%m)\n",
			mb->end, err);
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
	      oe_cmd ? oe_cmd->u.str : "",
	      oe_prm ? oe_prm->u.str : "",
	      oe_tok ? oe_tok->u.str : "");

	re_snprintf(buf, sizeof(buf), "%s%s%s",
		    oe_cmd->u.str,
		    oe_prm ? " " : "",
		    oe_prm ? oe_prm->u.str : "");

	/* Relay message to long commands */
	err = cmd_process_long(baresip_commands(),
			       buf,
			       str_len(buf),
			       &pf, NULL);
	if (err) {
		warning("ctrl_tcp: error processing command (%m)\n", err);
	}

	err = encode_response(resp, oe_tok ? oe_tok->u.str : NULL);
	if (err) {
		warning("ctrl_tcp: failed encode response (%m)\n", err);
		goto out;
	}

	resp->pos = 0;
	err = tcp_send(st->tc, resp);
	if (err) {
		warning("ctrl_tcp: failed to send the message (%m)\n", err);
	}

 out:
	mem_deref(resp);
	mem_deref(od);
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
	(void)tcp_accept(&st->tc, st->ts, NULL, tcp_recv_handler,
			 tcp_close_handler, st);
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

	err = odict_alloc(&od, 8);
	if (err)
		return;

	err = event_encode_dict(od, ua, ev, call, prm);
	if (err)
		goto out;

	err = json_encode_odict(&pf, od);
	if (err) {
		warning("ctrl_tcp: failed to encode json (%m)\n", err);
		goto out;
	}

	if (st->tc) {
		buf->pos = 0;
		err = tcp_send(st->tc, buf);

		if (err) {
			warning("ctrl_tcp: failed to send the message (%m)\n", err);
		}
	}

 out:
	mem_deref(buf);
	mem_deref(od);
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


const struct mod_export DECL_EXPORTS(ctrl) = {
	"ctrl_tcp",
	"application",
	ctrl_init,
	ctrl_close
};
