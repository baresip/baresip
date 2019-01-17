/**
 * @file mqtt.c Message Queue Telemetry Transport (MQTT) client
 *
 * Copyright (C) 2017 Creytiv.com
 */

#include <mosquitto.h>
#include <re.h>
#include <baresip.h>
#include "mqtt.h"


static char broker_host[256] = "127.0.0.1";
static uint32_t broker_port = 1883;

static struct mqtt s_mqtt;


static void fd_handler(int flags, void *arg)
{
	struct mqtt *mqtt = arg;

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
	(void)mqtt;

	if (result != MOSQ_ERR_SUCCESS) {
		warning("mqtt: could not connect to broker (%s)\n",
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


static int module_init(void)
{
	const int keepalive = 60;
	int ret;
	int err = 0;

	tmr_init(&s_mqtt.tmr);

	mosquitto_lib_init();

	conf_get_str(conf_cur(), "mqtt_broker_host",
		     broker_host, sizeof(broker_host));
	conf_get_u32(conf_cur(), "mqtt_broker_port", &broker_port);

	s_mqtt.mosq = mosquitto_new("baresip", true, &s_mqtt);
	if (!s_mqtt.mosq) {
		warning("mqtt: failed to create client instance\n");
		return ENOMEM;
	}

	err = mqtt_subscribe_init(&s_mqtt);
	if (err)
		return err;

	mosquitto_connect_callback_set(s_mqtt.mosq, connect_callback);

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

	err = fd_listen(s_mqtt.fd, FD_READ, fd_handler, &s_mqtt);
	if (err)
		return err;

	info("mqtt: module loaded\n");

	return err;
}


static int module_close(void)
{
	fd_close(s_mqtt.fd);

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
