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

// a buffer for events while not connected to mqtt yet
static struct list mqtt_event_buffer;
struct mqtt_event {
	struct le le;
	char *event;
    char *topic;
};

static void mqtt_event_destructor(void *arg)
{
	struct mqtt_event *e= arg;
	list_unlink(&e->le);
	mem_deref(e->event);
	mem_deref(e->topic);
}

/*
 * Relay UA events as publish messages to the Broker
 */
static void event_handler(enum ua_event ev, struct bevent *event, void *arg)
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
	if (ev == UA_EVENT_VU_RX) {
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

int publish_buffered_messages(struct mqtt *mqtt) {
	struct le *le;
	int ret;
	int err = 0;

	if (!mqtt->is_connected) {
		warning("mqtt: cannot publish queued messages in disconnected state\n");
        return 0;
    }

	LIST_FOREACH(&mqtt_event_buffer, le) {
		struct mqtt_event *e = (struct mqtt_event *)le->data;
		char *msg = e->event;
		char *topic = e->topic;

		warning("mqtt: publishing queued message (len=%d, data=%s)\n", (int)str_len(msg), msg);
        
		ret = mosquitto_publish(mqtt->mosq,
					NULL,
					topic,
					(int)str_len(msg),
					msg,
					mqtt->pubqos,
					false);
		if (ret != MOSQ_ERR_SUCCESS) {
			warning("mqtt: failed to publish queue entry (%s)\n",
				mosquitto_strerror(ret));
			err = EINVAL;
			goto err;
		}
	}
	list_flush(&mqtt_event_buffer);

err:
    return err;
}

int mqtt_publish_message(struct mqtt *mqtt, const char *topic,
			 const char *fmt, ...)
{
	char *message;
	va_list ap;
	int ret;
	int err = 0;
	struct le *le;
	struct pl topic_pl;

	if (!mqtt || !topic || !fmt)
		return EINVAL;

	va_start(ap, fmt);
	err = re_vsdprintf(&message, fmt, ap);
	va_end(ap);

	pl_set_str(&topic_pl, topic);

	if (err)
		return err;

	if (!mqtt->is_connected) {
		struct mqtt_event *e;

		warning("mqtt: trying to publish while not yet connected, queueing\n");

		e = mem_zalloc(sizeof(*e), mqtt_event_destructor);
		if (!e)
			return ENOMEM;
		e->event = message;
        pl_strdup(&e->topic, &topic_pl);
		list_append(&mqtt_event_buffer, &e->le, e);
		return 0;
	}

    err = publish_buffered_messages(mqtt);
	if (err)
		goto err;


	ret = mosquitto_publish(mqtt->mosq,
				NULL,
				topic,
				(int)str_len(message),
				message,
				mqtt->pubqos,
				false);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: failed to publish (%s)\n",
			mosquitto_strerror(ret));
		err = EINVAL;
		goto out;
	}

 out:
	mem_deref(message);
 err:
	return err;
}


int mqtt_publish_init(struct mqtt *mqtt)
{
	int err;

	err = bevent_register(event_handler, mqtt);
	if (err)
		return err;

	list_init(&mqtt_event_buffer);

	return 0;
}


void mqtt_publish_close(void)
{
	bevent_unregister(&event_handler);
	list_flush(&mqtt_event_buffer);
}
