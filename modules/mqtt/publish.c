/**
 * @file publish.c MQTT client -- publish
 *
 * Copyright (C) 2017 Alfred E. Heggestad
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
static void event_handler(enum bevent_ev ev, struct bevent *event, void *arg)
{
	struct mqtt *mqtt = arg;
	struct odict *od = NULL;
	struct call *call = bevent_get_call(event);
	int err;

	err = odict_alloc(&od, 8);
	if (err)
		return;

	err = odict_encode_bevent(od, event);
	if (err)
		goto out;

	/* send audio jitter buffer values together with VU rx values. */
	if (ev == BEVENT_VU_RX) {
		err = event_add_au_jb_stat(od,call);
		if (err) {
			info("Could not add audio jb value.\n");
		}
	}

	err = mqtt_publish_message(mqtt, mqtt->pubtopic, "%H",
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

	err = bevent_register(event_handler, mqtt);
	if (err)
		return err;

	return 0;
}


void mqtt_publish_close(void)
{
	bevent_unregister(&event_handler);
}
