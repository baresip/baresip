# baresip-webrtc
Baresip WebRTC Demo

1. Install libre

2. Install baresip dev:

`$ sudo make install install-dev -C ../baresip`

3. Compile this project:

`cmake . && make`

4. Start it:

```
$ ./baresip-webrtc -i stun:stun.l.google.com:19302
Local network addresses:
        en0:  172.20.10.3
medianat: ice
mediaenc: dtls_srtp
aucodec: opus/48000/2
aucodec: G722/16000/1
aucodec: PCMU/8000/1
aucodec: PCMA/8000/1
ausrc: ausine
vidcodec: H264
vidcodec: H264
vidcodec: H265
avcodec: using H.264 encoder 'libx264' -- libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
avcodec: using H.264 decoder 'h264' -- H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
avcodec: using H.265 encoder 'libx265' -- libx265 H.265 / HEVC
avcodec: using H.265 decoder 'hevc' -- HEVC (High Efficiency Video Coding)
vidcodec: VP8
vidcodec: VP9
ausrc: avformat
vidsrc: avformat
vidisp: sdl
vidsrc: fakevideo
vidisp: fakevideo
demo: listening on:
    http://172.20.10.3:9000/
    https://172.20.10.3:9001/
```

5. Open this URL in Chrome and follow the instructions:

`http://localhost:9000/`


## Protocol Diagram

This diagram shows how a WebRTC capable browser can connect to baresip-webrtc.
Baresip-WebRTC has a small embedded HTTP(S) Server for serving JavaScript files
and for signaling.

The media stream is compatible with WebRTC, using ICE and DTLS/SRTP as
media transport. The audio codecs are Opus, G722 or G711. The video codecs
are VP8, H264.

```
                  (Signaling)
.----------.       SDP/HTTP       .-----------.
| Browser  |<-------------------->|  Baresip  |
| (Chrome) |                      |  WebRTC   |<==== A/V Backend
|          |<====================>|           |
'----------'    ICE/DTLS/SRTP     '-----------'
                (Audio,Video)
```




## API Mapping


| WebRTC:               | this:                      |
| --------------------- | -------------------------- |
| MediaStream           | n/a                        |
| MediaStreamTrack      | struct media_track         |
| RTCConfiguration      | struct rtc_configuration   |
| RTCPeerConnection     | struct peer_connection     |
| RTCSessionDescription | struct session_description |
| RTCRtpTransceiver     | struct stream              |




## Signaling


```
.------.                                 .------.
|Client|                                 |Server|
'------'                                 '------'
    |             HTTP POST                  |
    +--------------------------------------->+
    |        201 Created (SDP offer)         |
    +<---------------------------------------+
    |                                        |
    |                                        |
    |        HTTP PUT (SDP Answer)           |
    +--------------------------------------->+
    |        200 OK                          |
    +<---------------------------------------+
    |                                        |
    |                                        |
    |        HTTP PATCH (ICE Candidate)      |
    +--------------------------------------->+
    |        200 OK                          |
    +<---------------------------------------+
    |                                        |
    |                                        |
    |                                        |
    |          ICE REQUEST                   |
    <========================================>
    |          ICE RESPONSE                  |
    <========================================>
    |          DTLS SETUP                    |
    <========================================>
    |          RTP/RTCP FLOW                 |
    <========================================>
    |                                        |
    |                                        |
    |                                        |
    |                                        |
    | HTTP DELETE                            |
    +--------------------------------------->+
    | 200 OK                                 |
    <----------------------------------------+


```


## Reference

https://www.ietf.org/archive/id/draft-ietf-wish-whip-03.html
