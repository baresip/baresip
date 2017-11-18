/**
 * @file subscribe.c MQTT client -- subscribe
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <mosquitto.h>
#include <re.h>
#include <baresip.h>
#include "mqtt.h"


static const char *subscription_pattern = "/baresip/+";


static int print_handler(const char *p, size_t size, void *arg)
{
	struct mbuf *mb = arg;

	return mbuf_write_mem(mb, (void *)p, size);
}


/*
 * This is called when a message is received from the broker.
 */
static void message_callback(struct mosquitto *mosq, void *obj,
			     const struct mosquitto_message *message)
{
	struct mqtt *mqtt = obj;
	struct mbuf *resp = mbuf_alloc(1024);
	struct re_printf pf = {print_handler, resp};
	struct pl msg;
	bool match = false;

	info("mqtt: got message '%b' for topic '%s'\n",
	     (char*) message->payload, (size_t)message->payloadlen,
	     message->topic);

	msg.p = message->payload;
	msg.l = message->payloadlen;

	mosquitto_topic_matches_sub("/baresip/command", message->topic,
				    &match);
	if (match) {

		info("mqtt: got message for '%s' topic\n", message->topic);

		/* XXX: add transaction ID ? */

		if (msg.l > 1 && msg.p[0] == '/') {

			/* Relay message to long commands */
			cmd_process_long(baresip_commands(),
					 &msg.p[1],
					 msg.l - 1,
					 &pf, NULL);

			/* NOTE: the command will now write the response
			         to the resp mbuf, send it back to broker */

			mqtt_publish_message(mqtt, "/baresip/command_resp",
					     "%b",
					     resp->buf, resp->end);
		}
		else {
			info("mqtt: message not handled (%r)\n", &msg);
		}
	}

	mem_deref(resp);
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

	ret = mosquitto_subscribe(mqtt->mosq, NULL, subscription_pattern, 0);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: failed to subscribe (%s)\n",
			mosquitto_strerror(ret));
		return EPROTO;
	}

	info("mqtt: subscribed to pattern '%s'\n", subscription_pattern);

	return 0;
}


void mqtt_subscribe_close(void)
{
}
