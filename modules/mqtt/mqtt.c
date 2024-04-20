/**
 * @file mqtt.c Message Queue Telemetry Transport (MQTT) client
 *
 * Copyright (C) 2017 Alfred E. Heggestad
 */

#include <mosquitto.h>
#include <re.h>
#include <baresip.h>
#include "mqtt.h"


static char broker_host[256] = "127.0.0.1";
/* Broker CA file for TLS usage, default none */
static char broker_cafile[256] = "";
/* Authentication user name, default none */
static char mqttusername[256] = "";
/* Authentication password, default none */
static char mqttpassword[256] = "";
/* Client ID - default "baresip" */
static char mqttclientid[256] = "baresip";
/* Base topic for MQTT - default "baresip" - i.e. /baresip/event */
static char mqttbasetopic[128] = "baresip";
static char mqttpublishtopic[256] = "";
static char mqttsubscribetopic[256] = "";

static uint32_t broker_port = 1883;

static struct mqtt s_mqtt;


static void fd_handler(int flags, void *arg)
{
	struct mqtt *mqtt = arg;
	(void)flags;

	mosquitto_loop_read(mqtt->mosq, 1);

	mosquitto_loop_write(mqtt->mosq, 1);
}


static void tmr_handler(void *data)
{
	struct mqtt *mqtt = data;
	int ret;

	tmr_start(&mqtt->tmr, 500, tmr_handler, mqtt);

	ret = mosquitto_loop_misc(mqtt->mosq);
	if (ret != MOSQ_ERR_SUCCESS) {
		warning("mqtt: error in loop (%s)\n", mosquitto_strerror(ret));
	}
}


/*
 * This is called when the broker sends a CONNACK message
 * in response to a connection.
 */
static void connect_callback(struct mosquitto *mosq, void *obj, int result)
{
	struct mqtt *mqtt = obj;
	int err;
	(void)mosq;

	if (result != MOSQ_ERR_SUCCESS) {
		warning("mqtt: could not connect to broker (%s) \n",
			mosquitto_strerror(result));
		return;
	}

	info("mqtt: connected to broker at %s:%d\n",
	     broker_host, broker_port);

	err = mqtt_subscribe_start(mqtt);
	if (err) {
		warning("mqtt: subscribe_init failed (%m)\n", err);
	}
}


static void tmr_reconnect(void *data)
{
	struct mqtt *mqtt = data;
	int err;

	err = mosquitto_reconnect(mqtt->mosq);
	if (err == MOSQ_ERR_SUCCESS) {
		mqtt->fd = mosquitto_socket(mqtt->mosq);

		err = fd_listen(&mqtt->fhs, mqtt->fd, FD_READ, fd_handler,
				mqtt);
		if (err) {
			warning("mqtt: reconnect fd_listen failed\n");
			return;
		}
		tmr_start(&mqtt->tmr, 500, tmr_handler, mqtt);
		info("mqtt: reconnected\n");
	}
	else {
		warning("mqtt: reconnect failed, will retry in 2 seconds\n");
		tmr_start(&mqtt->tmr, 2000, tmr_reconnect, mqtt);
	}
}


static void disconnect_callback(struct mosquitto *mosq, void *obj, int rc)
{
	struct mqtt *mqtt = obj;
	(void) mosq;

	/* Check for expected disconnect */
	if (rc == 0)
		return;

	warning("mqtt: connection lost (%s)\n", mosquitto_strerror(rc));
	tmr_cancel(&mqtt->tmr);
	mqtt->fhs = fd_close(mqtt->fhs);
	tmr_start(&mqtt->tmr, 1000, tmr_reconnect, mqtt);
}


static int module_init(void)
{
	const int keepalive = 60;
	int ret;
	int err = 0;

	tmr_init(&s_mqtt.tmr);

	mosquitto_lib_init();

	/* Get configuration data */
	conf_get_str(conf_cur(), "mqtt_broker_host",
		     broker_host, sizeof(broker_host));
	conf_get_str(conf_cur(), "mqtt_broker_cafile",
		     broker_cafile, sizeof(broker_cafile));
	conf_get_str(conf_cur(), "mqtt_broker_user",
		     mqttusername, sizeof(mqttusername));
	conf_get_str(conf_cur(), "mqtt_broker_password",
		     mqttpassword, sizeof(mqttpassword));
	conf_get_str(conf_cur(), "mqtt_broker_clientid",
		     mqttclientid, sizeof(mqttclientid));
	conf_get_str(conf_cur(), "mqtt_basetopic",
		     mqttbasetopic, sizeof(mqttbasetopic));
	conf_get_str(conf_cur(), "mqtt_publishtopic",
		     mqttpublishtopic, sizeof(mqttpublishtopic));
	conf_get_str(conf_cur(), "mqtt_subscribetopic",
		     mqttsubscribetopic, sizeof(mqttsubscribetopic));
	conf_get_u32(conf_cur(), "mqtt_broker_port", &broker_port);

	info("mqtt: connecting to broker at %s:%d as %s topic %s\n",
		broker_host, broker_port, mqttclientid, mqttbasetopic);

	if (*mqttsubscribetopic == '\0') {
		re_snprintf(mqttsubscribetopic, sizeof(mqttsubscribetopic),
				"/%s/command/+", mqttbasetopic);
	}
	if (*mqttpublishtopic == '\0') {
		re_snprintf(mqttpublishtopic, sizeof(mqttpublishtopic),
				"/%s/event", mqttbasetopic);
	}

	info("mqtt: Publishing on %s, subscribing to %s\n",
		mqttpublishtopic, mqttsubscribetopic);

	s_mqtt.basetopic = mqttbasetopic;
	s_mqtt.subtopic = mqttsubscribetopic;
	s_mqtt.pubtopic = mqttpublishtopic;


	s_mqtt.mosq = mosquitto_new(mqttclientid, true, &s_mqtt);
	if (!s_mqtt.mosq) {
		warning("mqtt: failed to create client instance\n");
		return ENOMEM;
	}

	err = mqtt_subscribe_init(&s_mqtt);
	if (err)
		return err;

	mosquitto_connect_callback_set(s_mqtt.mosq, connect_callback);
	mosquitto_disconnect_callback_set(s_mqtt.mosq, disconnect_callback);

	if (*mqttusername != '\0') {
		ret = mosquitto_username_pw_set(s_mqtt.mosq, mqttusername,
			mqttpassword);
		if (ret != MOSQ_ERR_SUCCESS)
			return ret == MOSQ_ERR_ERRNO ? errno : EIO;
	}

	if (*broker_cafile != '\0') {
		ret = mosquitto_tls_set(s_mqtt.mosq, broker_cafile,
				NULL, NULL, NULL, NULL);
		if (ret != MOSQ_ERR_SUCCESS)
			return ret == MOSQ_ERR_ERRNO ? errno : EIO;
	}

	ret = mosquitto_connect(s_mqtt.mosq, broker_host, broker_port,
				keepalive);
	if (ret != MOSQ_ERR_SUCCESS) {

		err = ret == MOSQ_ERR_ERRNO ? errno : EIO;

		warning("mqtt: failed to connect to %s:%d (%s)\n",
			broker_host, broker_port,
			mosquitto_strerror(ret));
		return err;
	}

	tmr_start(&s_mqtt.tmr, 1, tmr_handler, &s_mqtt);

	err = mqtt_publish_init(&s_mqtt);
	if (err)
		return err;

	s_mqtt.fd = mosquitto_socket(s_mqtt.mosq);
	s_mqtt.fhs = NULL;

	err = fd_listen(&s_mqtt.fhs, s_mqtt.fd, FD_READ, fd_handler, &s_mqtt);
	if (err)
		return err;

	info("mqtt: module loaded\n");

	return 0;
}


static int module_close(void)
{
	s_mqtt.fhs = fd_close(s_mqtt.fhs);

	mqtt_publish_close();

	mqtt_subscribe_close();

	tmr_cancel(&s_mqtt.tmr);

	if (s_mqtt.mosq) {

		mosquitto_disconnect(s_mqtt.mosq);

		mosquitto_destroy(s_mqtt.mosq);
		s_mqtt.mosq = NULL;
	}

	mosquitto_lib_cleanup();

	info("mqtt: module unloaded\n");

	return 0;
}


const struct mod_export DECL_EXPORTS(mqtt) = {
	"mqtt",
	"application",
	module_init,
	module_close
};
