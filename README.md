baresip README
==============


![Baresip Logo](https://raw.githubusercontent.com/baresip/baresip/master/share/logo.png)


Baresip is a portable and modular SIP User-Agent with audio and video support.  
Copyright (c) 2010 - 2025 Alfred E. Heggestad and Contributors  
Distributed under BSD license


![Build](https://github.com/baresip/baresip/workflows/Build/badge.svg)
![Lint](https://github.com/baresip/baresip/workflows/lint/badge.svg)
![OpenSSL and LibreSSL](https://github.com/baresip/baresip/workflows/OpenSSL%20no-deprecated%20and%20LibreSSL/badge.svg)
![Valgrind](https://github.com/baresip/baresip/workflows/valgrind%20leak%20check/badge.svg)


## Features:

* Call features:
  - Unlimited number of SIP accounts
  - Unlimited number of calls
  - Unattended call transfer
  - Auto answer
  - Call hold and resume
  - Microphone mute
  - Call waiting
  - Call recording
  - Peer to peer calls
  - Video calls
  - Instant Messaging
  - Custom ring tones
  - Repeat last call (redial)
  - Message Waiting Indication (MWI)
  - Address book with presence
  - Conferencing

* Signaling:
  - SIP protocol support
  - SIP outbound protocol for NAT-traversal
  - SIP Re-invite
  - SIP Routes
  - SIP early media support
  - DNS NAPTR/SRV support
  - Multiple accounts support
  - DTMF support (RTP, SIP INFO)
  - Multicast sending & receiving

* Security:
  - Signalling encryption (TLS)
  - Audio and video encryption (Secure RTP)
  - DTLS-SRTP key exchange protocol
  - ZRTP key exchange protocol
  - SDES key exchange protocol

* Audio:
  - Low latency audio pipeline
  - High definition audio codecs
  - Audio device configuration
  - Audio filter plugins
  - Internal audio resampler for fixed sampling rates
  - Linear 16 bit wave format support for ringtones
  - Packet loss concealment (PLC)
  - Configurable ringtone playback device
  - Automatic gain control (AGC) and Noise reducation
  - Acoustic echo control (AEC)
  - Configurable audio sample format (Signed 16-bit, 24-bit, Float etc)
  - EBU ACIP (Audio Contribution over IP) Profile

* Audio-codecs:
  - AAC
  - aptX
  - AMR narrowband, AMR wideband
  - Codec2
  - G.711
  - G.722
  - G.726
  - L16
  - Opus

* Audio-drivers:
  - Advanced Linux Sound Architecture (ALSA) audio-driver
  - PulseAudio POSIX OSes audio-driver
  - Android AAudio and OpenSLES audio-driver
  - Gstreamer playbin input audio-driver
  - JACK Audio Connection Kit audio-driver
  - MacOSX/iOS coreaudio/audiounit audio-driver
  - Portaudio audio-driver
  - Windows WASAPI audio-driver

* Video:
  - Support for H.264, H.265, VP8, VP9, AV1 Video
  - Configurable resolution/framerate/bitrate
  - Configurable video input/output
  - Support for asymmetric video
  - Configurable video pixel format
  - Hardware acceleration for video encoder/decoder

* Video-codecs:
  - AV1
  - H.264
  - H.265
  - VP8
  - VP9

* Video-drivers:
  - iOS avcapture video-source
  - FFmpeg/libav libavformat/avdevice input
  - Direct Show video-source
  - MacOSX AVCapture video-source
  - Linux V4L/V4L2 video-source
  - X11 grabber video-source
  - DirectFB video-output
  - SDL2 video-output
  - X11 video-output

* NAT-traversal:
  - STUN support
  - TURN server support
  - ICE support
  - NATPMP support
  - PCP (Port Control Protocol) support

* Networking:
  - multihoming, IPv4/IPv6
  - automatic network roaming

* Management:
  - Embedded web-server with HTTP interface
  - Command-line console over UDP/TCP
  - Command line interface (CLI)
  - Simple configuration files
  - MQTT (Message Queue Telemetry Transport) module

* Profiles:
  - EBU ACIP (Audio Contribution over IP) Profile


## Building

baresip is using CMake, and the following packages must be
installed before building:

* [libre](https://github.com/baresip/re)
* [openssl](https://www.openssl.org/)

See [Wiki: Install Stable Release](https://github.com/baresip/baresip/wiki/Install:-Stable-Release)
or [Wiki: Install GIT Version](https://github.com/baresip/baresip/wiki/Install:-GIT-Version)
for a full guide.

### Build with debug enabled

```
$ cmake -B build
$ cmake --build build -j
$ cmake --install build
```

### Build with release

```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release 
$ cmake --build build -j
```

### Build with selected modules

```
$ cmake -B build -DMODULES="menu;account;g711"
$ cmake --build build -j
```

### Build with custom app modules

```
$ cmake -B build -DAPP_MODULES_DIR=../baresip-apps/modules -DAPP_MODULES="auloop;vidloop"
$ cmake --build build -j
```

### Build with clang compiler

```
$ cmake -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
$ cmake --build build -j
```
### Build static

```
$ cmake -B build -DSTATIC=ON
$ cmake --build build -j
```

Modules will be built if external dependencies are installed.
After building you can start baresip like this:

```
$ build/baresip
```

The config files in `$HOME/.baresip` are automatically generated
the first time you run baresip.

### Build documentation

The API documentation can be build using [doxygen](https://www.doxygen.nl/manual/install.html).

```
$ doxygen mk/Doxyfile
```

By default the documentation is written to `../baresip-dox`, if you want to
change the destination directory you can change the `OUTPUT_DIRECTORY` in
`mk/Doxyfile`.

### Examples

* Configuration examples are available in the
[examples](https://github.com/baresip/baresip/tree/master/docs/examples)
directory.
* Documentation on configuring baresip can be found in the [Wiki](https://github.com/baresip/baresip/wiki/Configuration).

## License

The baresip project is using the 3-clause BSD license.


## Contributing

Patches can be sent via Github
[Pull-Requests](https://github.com/baresip/baresip/pulls) or to the Baresip
[mailing-list](https://groups.google.com/g/baresip).


## Design goals:

* Minimalistic and modular VoIP client
* SIP, SDP, RTP/RTCP, STUN/TURN/ICE
* IPv4 and IPv6 support
* RFC-compliancy
* Robust, fast, low footprint
* Portable C99 and C11 source code


## Modular Plugin Architecture:
```
aac           Advanced Audio Coding (AAC) audio codec
aaudio        Android AAudio driver
account       Account loader
alsa          ALSA audio driver
amr           Adaptive Multi-Rate (AMR) audio codec
aptx          Audio Processing Technology codec (aptX)
aubridge      Audio bridge module
auconv        Audio sample format converter
audiounit     AudioUnit audio driver for MacOSX/iOS
aufile        Audio module for using a WAV-file as audio input
augain        Module to adjust gain of audio source
auresamp      Audio resampler
ausine        Audio sine wave input module
av1           AV1 video codec
avcapture     Video source using iOS AVFoundation video capture
avcodec       Video codec using FFmpeg/libav libavcodec
avfilter      Video filter using FFmpeg libavfilter
avformat      Video source using FFmpeg/libav libavformat
codec2        Codec2 low bit rate speech codec
cons          UDP/TCP console UI driver
contact       Contacts module
coreaudio     Apple macOS Coreaudio driver
ctrl_dbus     Control interface using DBUS
ctrl_tcp      TCP control interface using JSON payload
debug_cmd     Debug commands
directfb      DirectFB video display module
dshow         Windows DirectShow video source
dtls_srtp     DTLS-SRTP end-to-end encryption
echo          Echo server module
evdev         Linux input driver
fakevideo     Fake video input/output driver
g711          G.711 audio codec
g722          G.722 audio codec
g7221         G.722.1 audio codec
g726          G.726 audio codec
gst           Gstreamer audio source
gtk           GTK+ 3 menu-based UI
gzrtp         ZRTP module using GNU ZRTP C++ library
httpd         HTTP webserver UI-module
httpreq       HTTP request module
ice           ICE protocol for NAT Traversal
in_band_dtmf  In-band DTMF decoder
jack          JACK Audio Connection Kit audio-driver
l16           L16 audio codec
menu          Interactive menu
mixausrc      Mixes another audio source into audio stream
mixminus      Mixes N-1 audio streams for conferencing
mqtt          MQTT (Message Queue Telemetry Transport) module
mwi           Message Waiting Indication
natpmp        NAT Port Mapping Protocol (NAT-PMP) module
netroam       Detects and applies changes of the local network addresses
opensles      OpenSLES audio driver
opus          OPUS Interactive audio codec
opus_multistream    OPUS multistream audio codec
pcp           Port Control Protocol (PCP) module
plc           Packet Loss Concealment (PLC) using spandsp
portaudio     Portaudio driver
pulse         Pulseaudio driver
presence      Presence module
rtcpsummary   RTCP summary module
sdl           Simple DirectMedia Layer 2.0 (SDL) video output driver
selfview      Video selfview module
serreg        Serial registration
snapshot      Save video-stream as PNG images
sndfile       Audio dumper using libsndfile
sndio         Audio driver for OpenBSD
srtp          Secure RTP encryption (SDES) using libre SRTP-stack
stdio         Standard input/output UI driver
stun          Session Traversal Utilities for NAT (STUN) module
swscale       Video scaling using libswscale
syslog        Syslog module
turn          Obtaining Relay Addresses from STUN (TURN) module
uuid          UUID generator and loader
v4l2          Video4Linux2 video source
vidbridge     Video bridge module
vidinfo       Video info overlay module
vp8           VP8 video codec
vp9           VP9 video codec
vumeter       Display audio levels in console
wasapi        Windows Audio Session API (WASAPI) driver
webrtc_aec    Acoustic Echo Cancellation (AEC) using WebRTC SDK
webrtc_aecm   Acoustic Echo Cancellation (AEC) mobile using WebRTC SDK
wincons       Console input driver for Windows
x11           X11 video output driver
```


## IETF RFC/I-Ds:

* RFC 3016  RTP Payload Format for MPEG-4 Audio/Visual Streams
* RFC 3262  Reliability of Provisional Responses for SIP
* RFC 3311  SIP UPDATE Method
* RFC 3428  SIP Extension for Instant Messaging
* RFC 3711  The Secure Real-time Transport Protocol (SRTP)
* RFC 3640  RTP Payload Format for Transport of MPEG-4 Elementary Streams
* RFC 3856  A Presence Event Package for SIP
* RFC 3863  Presence Information Data Format (PIDF)
* RFC 3891  The SIP "Replaces" Header
* RFC 4145  TCP-Based Media Transport in SDP
* RFC 4240  Basic Network Media Services with SIP (partly)
* RFC 4347  Datagram Transport Layer Security
* RFC 4568  SDP Security Descriptions for Media Streams
* RFC 4572  Connection-Oriented Media Transport over TLS Protocol in SDP
* RFC 4574  The SDP Label Attribute
* RFC 4585  Extended RTP Profile for RTCP-Based Feedback (RTP/AVPF)
* RFC 4587  RTP Payload Format for H.261 Video Streams
* RFC 4796  The SDP Content Attribute
* RFC 4867  RTP Payload Format for the AMR and AMR-WB Audio Codecs
* RFC 4961  Symmetric RTP / RTP Control Protocol (RTCP)
* RFC 5285  A General Mechanism for RTP Header Extensions
* RFC 5373  Requesting Answering Modes for SIP
* RFC 5506  Support for Reduced-Size RTCP
* RFC 5576  Source-Specific Media Attributes in SDP
* RFC 5577  RTP Payload Format for ITU-T Recommendation G.722.1
* RFC 5626  Managing Client-Initiated Connections in SIP
* RFC 5627  Obtaining and Using GRUUs in SIP
* RFC 5761  Multiplexing RTP Data and Control Packets on a Single Port
* RFC 5763  Framework for Establishing a SRTP Security Context Using DTLS
* RFC 5764  DTLS Extension to Establish Keys for SRTP
* RFC 5888  The SDP Grouping Framework
* RFC 6157  IPv6 Transition in SIP
* RFC 6184  RTP Payload Format for H.264 Video
* RFC 6263  App. Mechanism for Keeping Alive NAT Associated with RTP / RTCP
* RFC 6416  RTP Payload Format for MPEG-4 Audio/Visual Streams
* RFC 6464  A RTP Header Extension for Client-to-Mixer Audio Level Indication
* RFC 6716  Definition of the Opus Audio Codec
* RFC 6886  NAT Port Mapping Protocol (NAT-PMP)
* RFC 7064  URI Scheme for STUN Protocol
* RFC 7065  TURN Uniform Resource Identifiers
* RFC 7310  RTP Payload Format for Standard apt-X and Enhanced apt-X Codecs
* RFC 7587  RTP Payload Format for the Opus Speech and Audio Codec
* RFC 7741  RTP Payload Format for VP8 Video
* RFC 7742  WebRTC Video Processing and Codec Requirements
* RFC 7798  RTP Payload Format for High Efficiency Video Coding (HEVC)
* RFC 8285  A General Mechanism for RTP Header Extensions
* RFC 8843  Negotiating Media Multiplexing Using SDP

* draft-ietf-payload-vp9-16
* RTP Payload Format For AV1


## Supported platforms:

* Android (8.0 or later)
* Apple MacOS 11 and later (Xcode 10 or later)
* Apple iOS 10.0 or later
* Linux (kernel 4.0 or later, and glibc 2.31 or later)
* Windows 10 or later (mingw and VS2022)


### Supported versions of C Standard library

* Android bionic
* BSD libc
* GNU C Library (glibc)
* Musl
* Windows C Run-Time Libraries (CRT)
* uClibc


### Supported compilers:

* clang 9.x or later
* gcc 9.x or later
* MSVC 2022 or later


### Supported versions of OpenSSL

* OpenSSL version 3.x.x
* LibreSSL version 3.x


## Related projects

* [libre - baresip fork](https://github.com/baresip/re)
* [retest - baresip fork](https://github.com/baresip/retest)
* [libre](https://github.com/creytiv/re)
* [retest](https://github.com/creytiv/retest)


## References

* Github: https://github.com/baresip/baresip
* Mailing-list: https://groups.google.com/g/baresip
