/**
 * @file mqtt.h Message Queue Telemetry Transport (MQTT) client -- interface
 *
 * Copyright (C) 2017 Creytiv.com
 */


struct mqtt {
	struct mosquitto *mosq;
	struct tmr tmr;
	int fd;
};


/*
 * Subscribe direction (incoming)
 */

int  mqtt_subscribe_init(struct mqtt *mqtt);
int  mqtt_subscribe_start(struct mqtt *mqtt);
void mqtt_subscribe_close(void);


/*
 * Publish direction (outgoing)
 */

int  mqtt_publish_init(struct mqtt *mqtt);
void mqtt_publish_close(void);
int  mqtt_publish_message(struct mqtt *mqtt, const char *topic,
			  const char *fmt, ...);
