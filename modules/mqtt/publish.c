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


static int add_rtcp_stats(struct odict *od_parent, const struct rtcp_stats *rs)
{
	struct odict *od = NULL, *tx = NULL, *rx = NULL;
	int err = 0;

	if (!od_parent || !rs)
		return EINVAL;

	err  = odict_alloc(&od, 8);
	err |= odict_alloc(&tx, 8);
	err |= odict_alloc(&rx, 8);
	if (err)
		goto out;

	err  = odict_entry_add(tx, "sent", ODICT_INT, (int64_t)rs->tx.sent);
	err |= odict_entry_add(tx, "lost", ODICT_INT, (int64_t)rs->tx.lost);
	err |= odict_entry_add(tx, "jit", ODICT_INT, (int64_t)rs->tx.jit);
	if (err)
		goto out;

	err  = odict_entry_add(rx, "sent", ODICT_INT, (int64_t)rs->rx.sent);
	err |= odict_entry_add(rx, "lost", ODICT_INT, (int64_t)rs->rx.lost);
	err |= odict_entry_add(rx, "jit", ODICT_INT, (int64_t)rs->rx.jit);
	if (err)
		goto out;

	err  = odict_entry_add(od, "tx", ODICT_OBJECT, tx);
	err |= odict_entry_add(od, "rx", ODICT_OBJECT, rx);
	err |= odict_entry_add(od, "rtt", ODICT_INT, (int64_t)rs->rtt);
	if (err)
		goto out;

	/* add object to the parent */
	err = odict_entry_add(od_parent, "rtcp_stats", ODICT_OBJECT, od);
	if (err)
		goto out;

 out:
	mem_deref(od);

	return err;
}


/*
 * Relay UA events as publish messages to the Broker
 *
 * XXX: move JSON encoding to baresip core
 */
static void ua_event_handler(struct ua *ua, enum ua_event ev,
			     struct call *call, const char *prm, void *arg)
{
	struct mqtt *mqtt = arg;
	const char *event_str = uag_event_str(ev);
	struct odict *od = NULL;
	int err;

	err = odict_alloc(&od, 8);
	if (err)
		return;

	err |= odict_entry_add(od, "type", ODICT_STRING, event_str);
	err |= odict_entry_add(od, "accountaor", ODICT_STRING, ua_aor(ua));
	if (err)
		goto out;

	if (call) {

		const char *dir;

		dir = call_is_outgoing(call) ? "outgoing" : "incoming";

		err |= odict_entry_add(od, "direction", ODICT_STRING, dir);
		err |= odict_entry_add(od, "peeruri",
				       ODICT_STRING, call_peeruri(call));
		if (err)
			goto out;
	}

	if (str_isset(prm)) {
		err = odict_entry_add(od, "param", ODICT_STRING, prm);
		if (err)
			goto out;
	}

	if (ev == UA_EVENT_CALL_RTCP) {
		struct stream *strm = NULL;

		if (0 == str_casecmp(prm, "audio"))
			strm = audio_strm(call_audio(call));
		else if (0 == str_casecmp(prm, "video"))
			strm = video_strm(call_video(call));

		err = add_rtcp_stats(od, stream_rtcp_stats(strm));
		if (err)
			goto out;
	}

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
