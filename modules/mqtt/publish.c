/**
 * @file publish.c MQTT client -- publish
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <mosquitto.h>
#include <re.h>
#include <baresip.h>
#include "mqtt.h"


/*
 * This file contains functions for sending outgoing messages
 * from baresip to broker (publish)
 */


/*
 * Relay UA events as publish messages to the Broker
 */
static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct mqtt *mqtt = arg;
	struct odict *od = NULL;
	int err;

	err = odict_alloc(&od, 8);
	if (err)
		return;

	err = event_encode_dict(od, ua, ev, call, prm);
	if (err)
		goto out;

	err = mqtt_publish_message(mqtt, "/baresip/event", "%H",
				   json_encode_odict, od);
	if (err) {
		warning("mqtt: failed to publish message (%m)\n", err);
		goto out;
	}

 out:
	mem_deref(od);
}


int mqtt_publish_message(struct mqtt *mqtt, const char *topic,
			 const char *fmt, ...)
{
	char *message;
	va_list ap;
	int ret;
	int err = 0;

	if (!mqtt || !topic || !fmt)
		return EINVAL;

	va_start(ap, fmt);
	err = re_vsdprintf(&message, fmt, ap);
	va_end(ap);

	if (err)
		return err;

	ret = mosquitto_publish(mqtt->mosq,
				NULL,
				topic,
				(int)str_len(message),
				message,
				0,
				false);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: failed to publish (%s)\n",
			mosquitto_strerror(ret));
		err = EINVAL;
		goto out;
	}

 out:
	mem_deref(message);
	return err;
}


int mqtt_publish_init(struct mqtt *mqtt)
{
	int err;

	err = uag_event_register(ua_event_handler, mqtt);
	if (err)
		return err;

	return err;
}


void mqtt_publish_close(void)
{
	uag_event_unregister(&ua_event_handler);
}
