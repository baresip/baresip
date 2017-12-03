README
------


This module implements an MQTT (Message Queue Telemetry Transport) client
for publishing and subscribing to topics.


The module is using libmosquitto


Starting the MQTT broker:

```
$ /usr/local/sbin/mosquitto -v
```


Subscribing to all topics:

```
$ mosquitto_sub -t /baresip/+
```


Publishing to the topic:

```
$ mosquitto_pub -t /baresip/xxx -m foo=42
```


## Topic patterns

(Outgoing direction is from baresip mqtt module to broker,
 incoming direction is from broker to baresip mqtt module)

* /baresip/event         Outgoing events from ua_event
* /baresip/command       Incoming long command request
* /baresip/command_resp  Outgoing long command response


## Examples

```
/baresip/event sip:aeh@iptel.org,REGISTERING
/baresip/event sip:aeh@iptel.org,REGISTER_OK
/baresip/event sip:aeh@iptel.org,SHUTDOWN
```

```
mosquitto_pub -t /baresip/command -m "/dial music"

/baresip/command /dial music
/baresip/command_resp (null)
/baresip/event sip:aeh@iptel.org,CALL_ESTABLISHED
/baresip/event sip:aeh@iptel.org,CALL_CLOSED
```

