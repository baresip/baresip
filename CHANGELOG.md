# Baresip Changelog

All notable changes to baresip will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- aac: add AAC_STREAMTYPE_AUDIO enum value
- aac: add AAC_ prefix
- Video mode param to call_answer(), ua_answer() and ua_hold_answer [#966]
- video_stop_display() API function [#977]
- module: add path to module_load() function
- conf: add conf_configure_buf
- test: add usage of g711.so module [#978]
- JSON initial codec state command and response [#973]
- account_set_video_codecs() API function [#981]
- net: add fallback dns nameserver [#996]
- gtk: show call_peername in notify title [#1006]
- call: Added call_state() API function that returns enum state of the call [#1013]
- account_set_stun_user() and account_set_stun_pass() API functions [#1015]
- API functions account_stun_uri and account_set_stun_uri. [#1018]
- ausine: Audio sine wave input module [#1021]
- gtk/menu: replace spaces from uri [#1007]
- jack: allowing jack client name to be specified in the config file [#1025] [#1020]
- snapshot: Add snapshot_send and snapshot_recv commands [#1029]
- webrtc_aec: 'extended_filter' config option [#1030]
- avfilter: FFmpeg filter graphs integration [#1038]
- reg: view proxy expiry value in reg_status [#1068]
- account: add parameter rwait for re-register interval [#1069]
- call, stream, menu: add cmd to set the direction of video stream [#1073]

### Changed

- **Using [baresip/re](https://github.com/baresip/re) fork now**
- audio: move calculation to audio_jb_current_value
- avformat: clean up docs
- gzrtp: update docs
- account: increased size of audio codec list to 16
- video: make video_sdp_attr_decode public
- config: Derive default audio driver from default audio device [#1009]
- jack: modifying info message on jack client creation [#1019]
- call: when video stream is disabled, stop also video display [#1023]
- dtls_srtp: use tls_set_selfsigned_rsa with keysize 2048 [#1062] [#1056]
- rst: use a min ptime of 20ms
- aac: change ptime to 4ms

### Fixed

- avcodec: fix H.264 interop with Firefox
- winwave: waveInGetPosition is no longer supported for use as of Windows Vista [#960]
- avcodec: call av_hwdevice_ctx_create before if-statement
- account: use single quote instead of backtick
- ice: fix segfault in connh [#980]
- call: Update call->got_offer when re-INVITE or answer to re-INVITE
  is received [#986]
- mk: Test also for /usr/lib64/libspeexdsp.so to cover Fedora/RHEL/CentOS [#992]
- config: Allow distribution specific CA trust bundle locations (fixes [#993]
- config: Allow distribution specific default audio device (fixes [#994]
- mqtt: fix err is never read (found by clang static analyzer)
- avcodec: fix err is never read (found by clang static analyzer)
- gtk: notification buttons do not work on Systems [#1012]
- gtk: fix dtmf_tone and add tones as feedback [#1010]
- pulse: drain pulse buffers before freeing [#1016]
- jack: jack_play connect all physical ports [#1028]
- Makefile: do not try to install modules if build is static [#1031]
- gzrtp: media_alloc function is missing [#1034] [#1022]
- call: when updating video, check if video stream has been disabled [#1037]
- amr: fix length check, fixes [#1011]
- modules: fix search path for avdevice.h [#1043]
- gtk: declare variables C89 style
- config: init newly added member
- menu: fix segfault in ua_event_handler [#1059] [#1061]
- debug_cmd: fix OpenSSL no-deprecated [#1065]
- aac: handle missing bitrate parameter in SDP format
- av1: properly configure encoder

### Removed

- ice: remove support for ICE-lite
- ice: remove ice_debug, use log level DEBUG instead
- ice: make stun server optional
- config: remove ice_debug option (unused)

### Contributors (many thanks)

- Alfred E. Heggestad
- Alexander Gramner
- Andrew Webster
- Christian Spielberger
- Christoph Huber
- Davide Alberani
- Ethan Funk
- Juha Heinanen
- mbattista
- Michael Malone
- Mikl Kurkov
- ndilieto
- Robert Scheck
- Roger Sandholm
- Sebastian Reimers

[#966]: https://github.com/baresip/baresip/pull/966
[#977]: https://github.com/baresip/baresip/pull/977
[#978]: https://github.com/baresip/baresip/pull/978
[#973]: https://github.com/baresip/baresip/pull/973
[#981]: https://github.com/baresip/baresip/pull/981
[#996]: https://github.com/baresip/baresip/pull/996
[#1006]: https://github.com/baresip/baresip/pull/1006
[#1013]: https://github.com/baresip/baresip/pull/1013
[#1015]: https://github.com/baresip/baresip/pull/1015
[#1018]: https://github.com/baresip/baresip/pull/1018
[#1021]: https://github.com/baresip/baresip/pull/1021
[#1007]: https://github.com/baresip/baresip/pull/1007
[#1025]: https://github.com/baresip/baresip/pull/1025
[#1020]: https://github.com/baresip/baresip/pull/1020
[#1029]: https://github.com/baresip/baresip/pull/1029
[#1030]: https://github.com/baresip/baresip/pull/1030
[#1038]: https://github.com/baresip/baresip/pull/1038
[#1009]: https://github.com/baresip/baresip/pull/1009
[#1019]: https://github.com/baresip/baresip/pull/1019
[#1023]: https://github.com/baresip/baresip/pull/1023
[#1062]: https://github.com/baresip/baresip/pull/1062
[#1056]: https://github.com/baresip/baresip/pull/1056
[#960]: https://github.com/baresip/baresip/pull/960
[#980]: https://github.com/baresip/baresip/pull/980
[#986]: https://github.com/baresip/baresip/pull/986
[#992]: https://github.com/baresip/baresip/pull/992
[#993]: https://github.com/baresip/baresip/pull/993
[#994]: https://github.com/baresip/baresip/pull/994
[#1012]: https://github.com/baresip/baresip/pull/1012
[#1010]: https://github.com/baresip/baresip/pull/1010
[#1016]: https://github.com/baresip/baresip/pull/1016
[#1028]: https://github.com/baresip/baresip/pull/1028
[#1031]: https://github.com/baresip/baresip/pull/1031
[#1034]: https://github.com/baresip/baresip/pull/1034
[#1022]: https://github.com/baresip/baresip/pull/1022
[#1037]: https://github.com/baresip/baresip/pull/1037
[#1011]: https://github.com/baresip/baresip/pull/1011
[#1043]: https://github.com/baresip/baresip/pull/1043
[#1059]: https://github.com/baresip/baresip/pull/1059
[#1061]: https://github.com/baresip/baresip/pull/1061
[#1065]: https://github.com/baresip/baresip/pull/1065
[#1068]: https://github.com/baresip/baresip/pull/1068
[#1069]: https://github.com/baresip/baresip/pull/1069
[#1073]: https://github.com/baresip/baresip/pull/1073

[Unreleased]: https://github.com/baresip/baresip/compare/v0.6.6...HEAD
