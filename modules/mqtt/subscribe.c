/**
 * @file subscribe.c MQTT client -- subscribe
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <mosquitto.h>
#include <re.h>
#include <baresip.h>
#include "mqtt.h"


static int print_handler(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;

	return mbuf_write_mem(mb, (void *)p, size);
}


static void handle_command(struct mqtt *mqtt, const struct pl *msg)
{
	struct mbuf *resp = mbuf_alloc(1024);
	struct re_printf pf = {print_handler, resp};
	struct odict *od = NULL, *od_resp = NULL;
	const struct odict_entry *oe_cmd, *oe_prm, *oe_tok;
	char buf[256], resp_topic[256];
	const char *aor, *callid;
	int err;

	err = json_decode_odict(&od, 32, msg->p, msg->l, 16);
	if (err) {
		warning("mqtt: failed to decode JSON with %zu bytes (%m)\n",
			msg->l, err);
		return;
	}

	oe_cmd = odict_lookup(od, "command");
	oe_prm = odict_lookup(od, "params");
	oe_tok = odict_lookup(od, "token");
	if (!oe_cmd) {
		warning("mqtt: missing json entries\n");
		goto out;
	}

	aor    = odict_string(od, "accountaor");
	callid = odict_string(od, "callid");

	if (aor) {
		struct ua *ua = uag_find_aor(aor);
		if (!ua) {
			warning("mqtt: ua not found (%s)\n", aor);
			goto out;
		}

		uag_current_set(ua);

		if (callid) {
			struct call *call;

			call = call_find_id(ua_calls(ua), callid);
			if (!call) {
				warning("mqtt: call not found (%s)\n", callid);
				goto out;
			}

			call_set_current(ua_calls(ua), call);
		}
	}

	debug("mqtt: handle_command:  cmd='%s', token='%s'\n",
	      oe_cmd->u.str,
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
		warning("mqtt: error processing command (%m)\n", err);
	}

	err = mbuf_write_u8(resp, '\0');
	if (err)
		goto out;

	/* NOTE: the command will now write the response
	   to the resp mbuf, send it back to broker */

	re_snprintf(resp_topic, sizeof(resp_topic),
		    "/%s/command_resp/%s", mqtt->basetopic,
		    oe_tok ? oe_tok->u.str : "nil");

	err = odict_alloc(&od_resp, 8);
	if (err)
		goto out;

	err  = odict_entry_add(od_resp, "response", ODICT_BOOL, true);
	err |= odict_entry_add(od_resp, "ok", ODICT_BOOL, (bool)err==0);
	err |= odict_entry_add(od_resp, "data", ODICT_STRING, resp->buf);
	if (oe_tok) {
		err |= odict_entry_add(od_resp, "token",
				       ODICT_STRING, oe_tok->u.str);
	}
	if (err)
		goto out;

	err = mqtt_publish_message(mqtt, resp_topic,
				   "%H",
				   json_encode_odict, od_resp);
	if (err) {
		warning("mqtt: failed to publish message (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(resp);
	mem_deref(od_resp);
	mem_deref(od);
}


/*
 * This is called when a message is received from the broker.
 */
static void message_callback(struct mosquitto *mosq, void *obj,
			     const struct mosquitto_message *message)
{
	struct mqtt *mqtt = obj;
	struct pl msg;
	bool match = false;
	(void)mosq;

	info("mqtt: got message '%b' for topic '%s'\n",
	     (char*) message->payload, (size_t)message->payloadlen,
	     message->topic);

	msg.p = message->payload;
	msg.l = message->payloadlen;

	mosquitto_topic_matches_sub(mqtt->subtopic, message->topic,
				    &match);
	if (match) {
		info("mqtt: got message for '%s' topic\n", message->topic);

		handle_command(mqtt, &msg);
	}
}


int mqtt_subscribe_init(struct mqtt *mqtt)
{
	if (!mqtt)
		return EINVAL;

	mosquitto_message_callback_set(mqtt->mosq, message_callback);

	return 0;
}


int mqtt_subscribe_start(struct mqtt *mqtt)
{
	int ret;

	ret = mosquitto_subscribe(mqtt->mosq, NULL, mqtt->subtopic, 0);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: failed to subscribe (%s)\n",
			mosquitto_strerror(ret));
		return EPROTO;
	}

	info("mqtt: subscribed to pattern '%s'\n", mqtt->subtopic);

	return 0;
}


void mqtt_subscribe_close(void)
{
}
