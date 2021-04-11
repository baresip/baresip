# Baresip Changelog

All notable changes to baresip will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- cons: emulate key-release -- ref #1329
- Correct reverse domain name notation (#1342) [#1342]
- gtk with account_uri_complete (#1339) [#1339]
- bump version to 1.1.0 -- ref #1333
- ui: fix leaking of cmd_ctx (#1338) [#1338]
- DTMF tones for A B C D (#1340) [#1340]
- account: use a fixed username for the template
- contact: update contacts template
- config: disable ctrl_dbus in config template
- Module event (#1335) [#1335]
- add event UA_EVENT_MODULE to tell to app when snapshot has been written (#1330) [#1330]
- ringtone: generated busy and ringback tone (#1332) [#1332]
- audio: prevent restart of rx_thread on call termination (#1331) [#1331]
- modules: update auplay/ausrc modules
- Auplay remove inheritance (#1328) [#1328]
- h264: add doxygen comment
- vidloop: add VIDEO_SRATE
- vidloop: check error
- vidloop: add vidframe_clear
- vidloop: split enable_codec into encoder/decoder
- Ausrc remove inheritance (#1326) [#1326]
- ua: remove prev call (#1323) [#1323]
- sndfile: get number of bytes from auframe
- plc: check format of struct auframe
- speex_pp: check format of struct auframe
- webrtc_aec: use format from struct auframe
- README: update codecs and RFCs
- menu: use uri complete for command dialdir (#1321) [#1321]
- video: check for video display before calling handler
- Changed name and made public (#1319) [#1319]
- menu: return call-id for dial and dialdir (#1320) [#1320]
- Fixes for account uri complete (#1318) [#1318]
- Avoid compiler warnings:
- Avoid compiler warnings (I haven't found anything wrong with the code)
- vidfilt: fix warning
- vidfilt: split parameters into encode/decode
- snapshot: fix warnings
- video: group functions from vidutil.c
- avfilter: fix warnings
- vumeter: use format from audio frame
- replaced ua_uri_complete with account_uri_complete (#1317) [#1317]
- aulevel: move to librem
- omx: fix warning
- vidisp: remove inheritance (#1316) [#1316]
- docs: change video settings to match the default values (#1315) [#1315]
- menu: select call in cmd_find_call() (#1314) [#1314]
- menu: use menu_stop_play() (#1311) [#1311]
- main: unload app modules in signal handler (#1310) [#1310]
- avformat: replace const double with double
- avformat: clean up ifdefs (#1313) [#1313]
- ci: drop ubuntu 16.04 support - end of life
- avformat: proper code formatting
- avcodec: add avcodec prefix to log messages
- avcodec: check length of H265 packet
- x11grab: remove vidsrc inheritance
- v4l2: remove vs inheritance
- vidsrc: remove concept of baseclass/inheritance
- ua,menu: remove uag_find_call_state (#1304) [#1304]
- Updated homepage
- sdl: correct aspect-ratio in fullscreen mode
- vidloop: add vidisp parameters
- auloop: use auframe_size
- audio: use auframe_size
- Auplay use auframe (#1305) [#1305]
- Docs examples config (#1302) [#1302]
- Serreg fixes (#1301) [#1301]
- Update config.c [#1303]
- contact: use uag_find_requri()
- ua: use new tls function to set cafile and path [#1300]
- config: add sip_capath config line
- Call event answered fixes alsa issue (#1299) [#1299]
- ctrl_dbus: send DBUS signal when dbus interface is ready (#1296) [#1296]
- Multicast call priority (#1291) [#1291]
- Menu fixes for play tones2 (#1294) [#1294]
- gst: add missing include unistd.h [#1297]
- multicast: cleanup function description and fix doxygen warning (#1292) [#1292]
- menu: remove call resume for command hangup (#1289) [#1289]
- ua: add a generic filter API for calls (#1293) [#1293]
- Merge pull request #1288 from cspiel1/remove_call_resume_on_termination [#1288]
- menu: remove call resume on termination
- multicast: fix build error when using HAVE_PTHREAD=
- alsa_play.c add suggestion to use dmix (#1283) [#1283]
- readme.md: added multicast module (#1282) [#1282]
- audiounit: fix typo
- update copyright year (#1287) [#1287]
- config cleanup (#1286) [#1286]
- update copyright year (#1285) [#1285]
- conf: add call_hold_other_calls config option (#1280) [#1280]
- config.c: added rtmp to config template (#1284) [#1284]
- main.c: update year [#1281]
- The avformat_decoder should be optional (#1277) [#1277]
- src/audio: set started false with audio_stop (#1278) [#1278]
- readme: update baresip fork links
- ausine: mono support and stereo_left/right option [#1274]
- menu: fix incoming calls are not selected on call termination (#1271) [#1271]
- test: remove mock_aucodec, using g711 instead
- opengl: remove deprecated module (#1268) [#1268]
- Added account_dtmfmode and account_set_dtmfmode API functions (#1269) [#1269]
- avcodec: remove support for MPEG4 codec
- call: start streams asynchronously (issue #1261) (#1267) [#1267]
- audio: remove special handling of Comfort Noise
- multicast: fix one doxygen warning
- menu: update doxygen comment
- menu: correct hangupall command for parallel call feature (#1264) [#1264]
- menu: on call termination select another active call (#1260) [#1260]
- ua: correct doxygen of uag_hold_resume() [#1262]
- menu: simplify cmd_hangupall() (#1259) [#1259]
- support for sending of DTMF INFO (#1258) [#1258]
- Menu optional call parameter (#1254) [#1254]
- cleanup tabs and spaces [#1256]
- ua: correct doxygen for uag_hold_others()
- ua: add doxygen for call find functions
- menu: add doxygen to cmd_hangup(), cmd_hold(), cmd_resume()
- menu: command accept searches all User-Agents for an incoming call
- ua: add function uag_find_call_state()
- menu: print correct warning for hangup, accept, hold, resume
- menu: add optional parameter call-id to cmd_call_resume()
- menu: add optional parameter call-id to cmd_call_hold()
- menu: add optional parameter call-id to cmd_hangup()
- menu: add optional parameter call-id to cmd_answerdir()
- menu: add utility function that decodes complex command parameters
- menu: use SDP_SENDRECV for cmd_answerdir() as fallback
- menu: add optional parameter call-id to cmd_answer()
- ua: add call find per call-id function
- call: call_info() prints also the call-id
- ua: in ua_print_calls() print User-Agent info in header
- menu: ua NULL check for answer command
- replace spaces with tab [#1249]
- removed newline
- undid httpreq spacing
- fixed line too long
- moved multicast template to end of config template
- ua: fix uag_hold_others use of wrong list element [#1253]
- added multicast enabled message (#1251) [#1251]
- updated date and added multicast to signaling (#1252) [#1252]
- Merge pull request #1248 from webstean/patch-2 [#1248]
- Added newline to multicast comment
- Menu ensure only one established call (#1247) [#1247]
- Call resume on hangup (#1246) [#1246]
- menu: for call answer search all UAs for calls to put on hold
- ua: ua_answer() should answer same call like ua_hold_answer()
- ua: make ua_find_call_state() global usable
- Add multicast_listener to config template (#1245) [#1245]
- Update config template to include multicast module (#1244) [#1244]
- menu: if a call becomes established then put others on hold
- ua: add uag_hold_others()
- Fix multiple resumed calls (#1242) [#1242]
- Merge pull request #1241 from cHuberCoffee/cmd_hangupall [#1241]
- RFC: Make avformat decode mjpeg v4l2 with vaapi (#1216) [#1216]
- ua: add doxygen for new uag_hold_resume()
- menu: fix missing callid of menu at call closed
- menu: use uag_hold_resume to ensure only one active call
- ua: on call resume check for other active calls
- menu: new hangupall command with direction parameter
- readme: update supported compilers and ssl libs
- menu: fix redial
- Fix spaces
- Multicast module (#1231) [#1231]
- menu: use print backend pointer pf correctly (#1222) [#1222]
- menu: start ringback only once for parallel calls (#1238) [#1238]
- jack: support port pattern in config file (#1237) [#1237]
- config: disables server verification if sip_verify_server is missing (#1236) [#1236]
- ua: for UA selection allow arbitrary aor for regint=0 accounts (#1234) [#1234]
- Ctrl dbus synchronize (#1232) [#1232]
- event: encode also remote audio direction (#1227) [#1227]
- Merge pull request #1235 from cspiel1/event_add_string_for_UA_EVENT_CUSTOM [#1235]
- event: add string for UA_EVENT_CUSTOM
- Mimic ifdef on avutil version for hwcontext
- Fix to tabs and improve checks
- src/config: show sip_cafile warning only if sip_verify_server is enabled
- Avoid compiler warnings using casts [#1228]
- test: disable SIP TLS server verification [#1224]
- config,ua: add config flag disable SIP TLS server verification
- alsa/play: snd_pcm_writei error codes are negative
- alsa: fix clang warnings "conversion loses integer precision" [#1223]
- Intelligent call answer (#1218) [#1218]
- Remove uag next (#1207) [#1207]
- Merge pull request #1219 from cspiel1/message_reply_once [#1219]
- menu: update switch_audio_player
- Make vaapi/mjpeg options of avformat
- src/config: no sip_cafile wording
- message: reply only once
- src/ua: only warn if tls_add_ca fails, same as undefined cafile [#1214]
- src/config: add sip_cafile warning and enable by default
- ua: change log message from warning to info
- video: fix video payload text
- Make avformat decode mjpeg v4l2 with vaapi
- ua: improve UA selection for incoming calls (#1206) [#1206]
- ua: limit account matches for incoming calls to non-registrar accounts
- ua: check for NULL parameter in uag_find_msg()
- ua: early exit for AF_UNSPEC in uri_match_af()
- ua: use sip_transp_decode() in uri_match_transport()
- ua: use arrays in uri_host_local()
- test: add test for deny UDP peer-to-peer call
- ua: improve UA selection for incoming calls
- Sip message to application (#1201) [#1201]
- opus: Ensure (re)init of fmtp strings (#1209) [#1209]
- ctrl_dbus: generate dbus interface during build (#1208) [#1208]
- mod_gtk: switch to gtk 3 (#1203) [#1203]
- menu: set_answer_mode: apply all uas
- menu: find_call: search all user-agents
- menu: fix usage of ua
- isac: remove deprecated module (#1204) [#1204]
- menu: cmd_print_calls: print all uas
- Fix interaction between CLI menu and GTK menu (#1202) [#1202]
- menu: rename menu_current() to menu_uacur()
- webrtc_aec: fix compilation with gcc 4.9 (fix #1193)
- win32: add cons module, fixes #1197
- ua: remove ua_aor() -- use account_aor() instead
- gtk: use account_aor()
- menu: use account_aor()
- presence: use account_aor()
- modules: use account_aor()
- account: fix video codes decode (#1196) [#1196]
- core: use account_aor()
- Merge pull request #1198 from baresip/av1 [#1198]
- Avoid unused parameter warning
- debug_cmd: add UA_EVENT_CUSTOM (#1194) [#1194]
- fix decoder changed debug text
- cairo: minor debug tuning
- menu: add uadelall to delete all user agents [#1195]
- use account_aor()
- mctrl: remove support for media-control (deprecated)
- update doxygen comments
- ua: minor cleanup
- ua: split struct uag from instance
- README: add RFC 5373
- menu: fix segfault on last account deletion (#1192) [#1192]
- call: extend SIP auto answer support for incoming calls (#1191) [#1191]
- Sip auto answer caller (#1188) [#1188]
- win32: remove timer.c
- ua: give a nice name to 'global' struct
- ua: remove ua_cur
- move uag_current to menu module
- menu: pass ua from mqtt to menu via opaque data
- Sip autoanswer callee (#1187) [#1187]
- ua: for answer-mode early also send INCOMING event (#1185) [#1185]
- gst: The error handler call for end of stream is now (#1182) [#1182]
- mk: also detect mqtt.so in SYSROOT_ALT
- contact: add ua_lookup_domain
- video: minor tuning of pipeline text
- gst: playback of read only audio files failed (#1183) [#1183]
- gtk: make a local pointer to current ua
- menu: clean up usage of uag_current()
- call: correction of remote video direction info at SDP-offer (#1181) [#1181]
- debug_cmd: print all user-agents
- presence: one command with status as argument
- ua: rename presence status to pstat
- ua: remove LIBRE_HAVE_SIPTRACE check, always enabled
- update doxygen comments
- mk: update doxygen config file
- menu: initialize menu with zeros (#1179) [#1179]
- Re mk cross build2 (#1161) [#1161]
- net: make fallback DNS ignored message debug only
- mixausrc: improve logging [#1176]
- mixausrc: fix shorten-64-to-32 warnings
- config: template for osx/ios
- Supressed clang zero length array warning
- Added ctx param to video_stop/video_stop_source and set ctx to null (#1173) [#1173]
- avformat: add empty line after base class
- Make macos warnings into errors (#1171) [#1171]
- disable mixausrc until warnings are fixed
- clang shorten-64-to-32 warnings (#1170) [#1170]
- Mixausrc (#1159) [#1159]
- aufile: fix warning on OSX
- alsa: print warning if running, fixed #1162
- Don't default stunuser/pass to account authuser/pass (#1164) [#1164]
- Audio file info (#1157) [#1157]
- gitignore: clangd cache, compile_commands.json and cleanup
- Merge pull request #1167 from baresip/video_display [#1167]
- Reordered video_stop_display
- Expose video_stop_display() to API
- Video dir rename (#1158) [#1158]
- ci: use baresip/rem repo
- stream: add function to send a RTP dummy packet (#1156) [#1156]
- Play aufile extended support (#1155) [#1155]
- video: move video related start/stop/update into video file (#1151) [#1151]
- aufile: add audio player to write speaker data to wav file (#1153) [#1153]
- Fix compiler warnings (#1152) [#1152]
- play: fix warning
- play ausrc (#1147) [#1147]
- README: add more status badges
- README: replace travis status badge
- menu: fix uint16_t scode [#1149]
- config: revert dirent.h changes
- audio: fix HAVE_PTHREAD audio_destructor
- gst ready for file play (#1148) [#1148]
- debug_cmd: mem_deref of player fixes segfault (#1146) [#1146]
- net: remove deprecated net_domain()
- update contact examples
- fix freeze on hangup (#1135) (#1145) [#1145]
- menu: make audio files configurable (#1144) [#1144]
- aptx: declare variable outside for-loop
- fix warnings on openbsd
- jack: declare variable outside for loop
- account: declare variable outside for loop
- coreaudio: declare variable outside for loop
- menu: initialize menu.play fixes segfault (#1143) [#1143]
- ausine: declare variable outside for loop
- timer: remove tmr_jiffies_usec (replaced by libre) (#1141) [#1141]
- Adaptive jbuf (#1112) [#1112]
- Update build.yml (#1140) [#1140]
- mqtt: allow to separate pub from sub topic base (#1139) [#1139]
- video: fix warning
- mqtt: fix printing port and add tls support (#1138) [#1138]
- httpreq: in cmd_setauth check if parameter was given (#1134) [#1134]
- Merge pull request #1132 from baresip/pr-dependency-action [#1132]
- ci: add pull request dependency checkouts
- audio: remove redundant union
- menu: use menu_ as prefix for global symbols
- menu: use menu_ as prefix for global symbols
- ci: add apt-get update
- menu: module refactoring (#1129) [#1129]
- audio, video, stream: check payload type before put to jbuf (#1128) [#1128]
- Cmd dialdir (#1126) [#1126]
- Cmd acceptdir (#1125) [#1125]
- event: add register fallback to event string and class name (#1124) [#1124]
- avformat: use %u for unsigned
- modify event type and check if peeruri null  (#1119) [#1119]
- event: move code from ua.c (#1118) [#1118]
- Valgrind ci (#1117) [#1117]
- h264 cleanup, second part (#1115) [#1115]
- h264 cleanup (#1114) [#1114]
- Merge pull request #1113 from baresip/github-actions-v2 [#1113]
- ci: remove travis
- ci: add github actions - replaces travisci
- qtcapture: remove deprecated module (#1107) [#1107]
- test: prepare for dualstack
- test: add mock dns_server_add_aaaa
- make EXTRA_MODULES last, not first (#1106) [#1106]
- httpreq: fix cmd_settimeout
- test: bind network to localhost, a fix for #1090
- modules/webrtc_aec: link flags fixes (#1105) [#1105]
- menu: commands in alphabetical order
- httpreq: fix warning about unused args
- serreg: fix warnings about unused argument
- menu: fix warnings about unused argument
- Add a HTTP request module with authorization (#1099) [#1099]
- Menu: corrections for ring tones and call status by means of a global call counter (#1102) [#1102]
- mk: remove dirent.h
- Updating .vcxproj file for windows builds (#1097) [#1097]
- ccheck: change license to BSD license
- Merge pull request #1095 from baresip/websocket [#1095]
- Serial registration (#1083) [#1083]
- Ctrl dbus (#1085) [#1085]
- README: remove references to creytiv.com
- Branch of baresip that includes Alfred's sip websocket patch
- Merge pull request #1091 from baresip/debian [#1091]
- ua, menu: new command to print certificate issuer and subject (#1078) [#1078]
- .gitignore: add ctags and Vim swp files (#1084) [#1084]


### Contributors (many thanks)

- [alfredh](https://github.com/alfredh)
- [robert-scheck](https://github.com/robert-scheck)
- [mbattista](https://github.com/mbattista)
- [cspiel1](https://github.com/cspiel1)
- [juha-h](https://github.com/juha-h)
- [ahinrichs](https://github.com/ahinrichs)
- [jurjen-van-dijk](https://github.com/jurjen-van-dijk)
- [sreimers](https://github.com/sreimers)
- [cHuberCoffee](https://github.com/cHuberCoffee)
- [webstean](https://github.com/webstean)
- [viric](https://github.com/viric)
- [agramner](https://github.com/agramner)
- [weili-jiang](https://github.com/weili-jiang)
- [thillux](https://github.com/thillux)
- [wkiswk](https://github.com/wkiswk)
- [philippbachmann08](https://github.com/philippbachmann08)
- [ursfassler](https://github.com/ursfassler)
- [RobertMi21](https://github.com/RobertMi21)
- [alberanid](https://github.com/alberanid)
- [agranig](https://github.com/agranig)
- [nanguantong](https://github.com/nanguantong)
- [johnjuuljensen](https://github.com/johnjuuljensen)


## [1.0.0] - 2020-09-11

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
- Added AMRWBENC_PATH env var to amr module module.mk [#1081]

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
- call: When terminating outgoing call, terminate also possible refer
  subscription [#1082]
- menu: fix segfault in /aubitrate command
- amr: should check if file (instead of directory) exists

### Removed

- ice: remove support for ICE-lite
- ice: remove ice_debug, use log level DEBUG instead
- ice: make stun server optional
- config: remove ice_debug option (unused)
- opengles: remove module (not working) [#1079]

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


[#960]: https://github.com/baresip/baresip/pull/960
[#966]: https://github.com/baresip/baresip/pull/966
[#973]: https://github.com/baresip/baresip/pull/973
[#977]: https://github.com/baresip/baresip/pull/977
[#978]: https://github.com/baresip/baresip/pull/978
[#980]: https://github.com/baresip/baresip/pull/980
[#981]: https://github.com/baresip/baresip/pull/981
[#986]: https://github.com/baresip/baresip/pull/986
[#992]: https://github.com/baresip/baresip/pull/992
[#993]: https://github.com/baresip/baresip/pull/993
[#994]: https://github.com/baresip/baresip/pull/994
[#996]: https://github.com/baresip/baresip/pull/996
[#1006]: https://github.com/baresip/baresip/pull/1006
[#1007]: https://github.com/baresip/baresip/pull/1007
[#1009]: https://github.com/baresip/baresip/pull/1009
[#1010]: https://github.com/baresip/baresip/pull/1010
[#1011]: https://github.com/baresip/baresip/pull/1011
[#1012]: https://github.com/baresip/baresip/pull/1012
[#1013]: https://github.com/baresip/baresip/pull/1013
[#1015]: https://github.com/baresip/baresip/pull/1015
[#1016]: https://github.com/baresip/baresip/pull/1016
[#1018]: https://github.com/baresip/baresip/pull/1018
[#1019]: https://github.com/baresip/baresip/pull/1019
[#1020]: https://github.com/baresip/baresip/pull/1020
[#1021]: https://github.com/baresip/baresip/pull/1021
[#1022]: https://github.com/baresip/baresip/pull/1022
[#1023]: https://github.com/baresip/baresip/pull/1023
[#1025]: https://github.com/baresip/baresip/pull/1025
[#1028]: https://github.com/baresip/baresip/pull/1028
[#1029]: https://github.com/baresip/baresip/pull/1029
[#1030]: https://github.com/baresip/baresip/pull/1030
[#1031]: https://github.com/baresip/baresip/pull/1031
[#1034]: https://github.com/baresip/baresip/pull/1034
[#1037]: https://github.com/baresip/baresip/pull/1037
[#1038]: https://github.com/baresip/baresip/pull/1038
[#1043]: https://github.com/baresip/baresip/pull/1043
[#1056]: https://github.com/baresip/baresip/pull/1056
[#1059]: https://github.com/baresip/baresip/pull/1059
[#1061]: https://github.com/baresip/baresip/pull/1061
[#1062]: https://github.com/baresip/baresip/pull/1062
[#1065]: https://github.com/baresip/baresip/pull/1065
[#1068]: https://github.com/baresip/baresip/pull/1068
[#1069]: https://github.com/baresip/baresip/pull/1069
[#1073]: https://github.com/baresip/baresip/pull/1073
[#1078]: https://github.com/baresip/baresip/pull/1078
[#1079]: https://github.com/baresip/baresip/pull/1079
[#1081]: https://github.com/baresip/baresip/pull/1081
[#1082]: https://github.com/baresip/baresip/pull/1082
[#1083]: https://github.com/baresip/baresip/pull/1083
[#1084]: https://github.com/baresip/baresip/pull/1084
[#1085]: https://github.com/baresip/baresip/pull/1085
[#1091]: https://github.com/baresip/baresip/pull/1091
[#1095]: https://github.com/baresip/baresip/pull/1095
[#1097]: https://github.com/baresip/baresip/pull/1097
[#1099]: https://github.com/baresip/baresip/pull/1099
[#1102]: https://github.com/baresip/baresip/pull/1102
[#1105]: https://github.com/baresip/baresip/pull/1105
[#1106]: https://github.com/baresip/baresip/pull/1106
[#1107]: https://github.com/baresip/baresip/pull/1107
[#1112]: https://github.com/baresip/baresip/pull/1112
[#1113]: https://github.com/baresip/baresip/pull/1113
[#1114]: https://github.com/baresip/baresip/pull/1114
[#1115]: https://github.com/baresip/baresip/pull/1115
[#1117]: https://github.com/baresip/baresip/pull/1117
[#1118]: https://github.com/baresip/baresip/pull/1118
[#1119]: https://github.com/baresip/baresip/pull/1119
[#1124]: https://github.com/baresip/baresip/pull/1124
[#1125]: https://github.com/baresip/baresip/pull/1125
[#1126]: https://github.com/baresip/baresip/pull/1126
[#1128]: https://github.com/baresip/baresip/pull/1128
[#1129]: https://github.com/baresip/baresip/pull/1129
[#1132]: https://github.com/baresip/baresip/pull/1132
[#1134]: https://github.com/baresip/baresip/pull/1134
[#1138]: https://github.com/baresip/baresip/pull/1138
[#1139]: https://github.com/baresip/baresip/pull/1139
[#1140]: https://github.com/baresip/baresip/pull/1140
[#1141]: https://github.com/baresip/baresip/pull/1141
[#1143]: https://github.com/baresip/baresip/pull/1143
[#1144]: https://github.com/baresip/baresip/pull/1144
[#1145]: https://github.com/baresip/baresip/pull/1145
[#1146]: https://github.com/baresip/baresip/pull/1146
[#1147]: https://github.com/baresip/baresip/pull/1147
[#1148]: https://github.com/baresip/baresip/pull/1148
[#1149]: https://github.com/baresip/baresip/pull/1149
[#1151]: https://github.com/baresip/baresip/pull/1151
[#1152]: https://github.com/baresip/baresip/pull/1152
[#1153]: https://github.com/baresip/baresip/pull/1153
[#1155]: https://github.com/baresip/baresip/pull/1155
[#1156]: https://github.com/baresip/baresip/pull/1156
[#1157]: https://github.com/baresip/baresip/pull/1157
[#1158]: https://github.com/baresip/baresip/pull/1158
[#1159]: https://github.com/baresip/baresip/pull/1159
[#1161]: https://github.com/baresip/baresip/pull/1161
[#1164]: https://github.com/baresip/baresip/pull/1164
[#1167]: https://github.com/baresip/baresip/pull/1167
[#1170]: https://github.com/baresip/baresip/pull/1170
[#1171]: https://github.com/baresip/baresip/pull/1171
[#1173]: https://github.com/baresip/baresip/pull/1173
[#1176]: https://github.com/baresip/baresip/pull/1176
[#1179]: https://github.com/baresip/baresip/pull/1179
[#1181]: https://github.com/baresip/baresip/pull/1181
[#1182]: https://github.com/baresip/baresip/pull/1182
[#1183]: https://github.com/baresip/baresip/pull/1183
[#1185]: https://github.com/baresip/baresip/pull/1185
[#1187]: https://github.com/baresip/baresip/pull/1187
[#1188]: https://github.com/baresip/baresip/pull/1188
[#1191]: https://github.com/baresip/baresip/pull/1191
[#1192]: https://github.com/baresip/baresip/pull/1192
[#1194]: https://github.com/baresip/baresip/pull/1194
[#1195]: https://github.com/baresip/baresip/pull/1195
[#1196]: https://github.com/baresip/baresip/pull/1196
[#1198]: https://github.com/baresip/baresip/pull/1198
[#1201]: https://github.com/baresip/baresip/pull/1201
[#1202]: https://github.com/baresip/baresip/pull/1202
[#1203]: https://github.com/baresip/baresip/pull/1203
[#1204]: https://github.com/baresip/baresip/pull/1204
[#1206]: https://github.com/baresip/baresip/pull/1206
[#1207]: https://github.com/baresip/baresip/pull/1207
[#1208]: https://github.com/baresip/baresip/pull/1208
[#1209]: https://github.com/baresip/baresip/pull/1209
[#1214]: https://github.com/baresip/baresip/pull/1214
[#1216]: https://github.com/baresip/baresip/pull/1216
[#1218]: https://github.com/baresip/baresip/pull/1218
[#1219]: https://github.com/baresip/baresip/pull/1219
[#1222]: https://github.com/baresip/baresip/pull/1222
[#1223]: https://github.com/baresip/baresip/pull/1223
[#1224]: https://github.com/baresip/baresip/pull/1224
[#1227]: https://github.com/baresip/baresip/pull/1227
[#1228]: https://github.com/baresip/baresip/pull/1228
[#1231]: https://github.com/baresip/baresip/pull/1231
[#1232]: https://github.com/baresip/baresip/pull/1232
[#1234]: https://github.com/baresip/baresip/pull/1234
[#1235]: https://github.com/baresip/baresip/pull/1235
[#1236]: https://github.com/baresip/baresip/pull/1236
[#1237]: https://github.com/baresip/baresip/pull/1237
[#1238]: https://github.com/baresip/baresip/pull/1238
[#1241]: https://github.com/baresip/baresip/pull/1241
[#1242]: https://github.com/baresip/baresip/pull/1242
[#1244]: https://github.com/baresip/baresip/pull/1244
[#1245]: https://github.com/baresip/baresip/pull/1245
[#1246]: https://github.com/baresip/baresip/pull/1246
[#1247]: https://github.com/baresip/baresip/pull/1247
[#1248]: https://github.com/baresip/baresip/pull/1248
[#1249]: https://github.com/baresip/baresip/pull/1249
[#1251]: https://github.com/baresip/baresip/pull/1251
[#1252]: https://github.com/baresip/baresip/pull/1252
[#1253]: https://github.com/baresip/baresip/pull/1253
[#1254]: https://github.com/baresip/baresip/pull/1254
[#1256]: https://github.com/baresip/baresip/pull/1256
[#1258]: https://github.com/baresip/baresip/pull/1258
[#1259]: https://github.com/baresip/baresip/pull/1259
[#1260]: https://github.com/baresip/baresip/pull/1260
[#1262]: https://github.com/baresip/baresip/pull/1262
[#1264]: https://github.com/baresip/baresip/pull/1264
[#1267]: https://github.com/baresip/baresip/pull/1267
[#1268]: https://github.com/baresip/baresip/pull/1268
[#1269]: https://github.com/baresip/baresip/pull/1269
[#1271]: https://github.com/baresip/baresip/pull/1271
[#1274]: https://github.com/baresip/baresip/pull/1274
[#1277]: https://github.com/baresip/baresip/pull/1277
[#1278]: https://github.com/baresip/baresip/pull/1278
[#1280]: https://github.com/baresip/baresip/pull/1280
[#1281]: https://github.com/baresip/baresip/pull/1281
[#1282]: https://github.com/baresip/baresip/pull/1282
[#1283]: https://github.com/baresip/baresip/pull/1283
[#1284]: https://github.com/baresip/baresip/pull/1284
[#1285]: https://github.com/baresip/baresip/pull/1285
[#1286]: https://github.com/baresip/baresip/pull/1286
[#1287]: https://github.com/baresip/baresip/pull/1287
[#1288]: https://github.com/baresip/baresip/pull/1288
[#1289]: https://github.com/baresip/baresip/pull/1289
[#1291]: https://github.com/baresip/baresip/pull/1291
[#1292]: https://github.com/baresip/baresip/pull/1292
[#1293]: https://github.com/baresip/baresip/pull/1293
[#1294]: https://github.com/baresip/baresip/pull/1294
[#1296]: https://github.com/baresip/baresip/pull/1296
[#1297]: https://github.com/baresip/baresip/pull/1297
[#1299]: https://github.com/baresip/baresip/pull/1299
[#1300]: https://github.com/baresip/baresip/pull/1300
[#1301]: https://github.com/baresip/baresip/pull/1301
[#1302]: https://github.com/baresip/baresip/pull/1302
[#1303]: https://github.com/baresip/baresip/pull/1303
[#1304]: https://github.com/baresip/baresip/pull/1304
[#1305]: https://github.com/baresip/baresip/pull/1305
[#1310]: https://github.com/baresip/baresip/pull/1310
[#1311]: https://github.com/baresip/baresip/pull/1311
[#1313]: https://github.com/baresip/baresip/pull/1313
[#1314]: https://github.com/baresip/baresip/pull/1314
[#1315]: https://github.com/baresip/baresip/pull/1315
[#1316]: https://github.com/baresip/baresip/pull/1316
[#1317]: https://github.com/baresip/baresip/pull/1317
[#1318]: https://github.com/baresip/baresip/pull/1318
[#1319]: https://github.com/baresip/baresip/pull/1319
[#1320]: https://github.com/baresip/baresip/pull/1320
[#1321]: https://github.com/baresip/baresip/pull/1321
[#1323]: https://github.com/baresip/baresip/pull/1323
[#1326]: https://github.com/baresip/baresip/pull/1326
[#1328]: https://github.com/baresip/baresip/pull/1328
[#1330]: https://github.com/baresip/baresip/pull/1330
[#1331]: https://github.com/baresip/baresip/pull/1331
[#1332]: https://github.com/baresip/baresip/pull/1332
[#1335]: https://github.com/baresip/baresip/pull/1335
[#1338]: https://github.com/baresip/baresip/pull/1338
[#1339]: https://github.com/baresip/baresip/pull/1339
[#1340]: https://github.com/baresip/baresip/pull/1340
[#1342]: https://github.com/baresip/baresip/pull/1342


[Unreleased]: https://github.com/baresip/baresip/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/baresip/baresip/compare/v0.6.6...v1.0.0
