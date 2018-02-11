README
------


This module implements an MQTT (Message Queue Telemetry Transport) client
for publishing and subscribing to topics.


The module is using libmosquitto. All messages are encoded in JSON format.


Starting the MQTT broker:

```
$ /usr/local/sbin/mosquitto -v
```


Subscribing to all topics:

```
$ mosquitto_sub -v -t /baresip/#
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
/baresip/event {"type":"REGISTERING","class":"register","accountaor":"sip:aeh@iptel.org"}
/baresip/event {"type":"REGISTER_OK","class":"register","accountaor":"sip:aeh@iptel.org","param":"200 OK"}
/baresip/event {"type":"SHUTDOWN","class":"application","accountaor":"sip:aeh@iptel.org"}
```

```
mosquitto_pub -t /baresip/command -m "/dial music"

/baresip/command {"command":"dial","params":"music","token":"123"}
/baresip/command_resp/123 (null)
/baresip/event {"type":"CALL_ESTABLISHED","class":"call","accountaor":"sip:aeh@iptel.org","direction":"outgoing","peeruri":"sip:music@iptel.org","id":"4d758140c42c5d55","param":"sip:music@iptel.org"}
/baresip/event {"type":"CALL_CLOSED","class":"call","accountaor":"sip:aeh@iptel.org","direction":"outgoing","peeruri":"sip:music@iptel.org","id":"4d758140c42c5d55","param":"Connection reset by user"}
```
