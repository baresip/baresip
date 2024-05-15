# Baresip Changelog

All notable changes to baresip will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 3.12.0 - 2024-05-15

## What's Changed
* video: stream enable/disable for re-INVITE/UPDATE by @cspiel1 in https://github.com/baresip/baresip/pull/2982
* srtp: allow rekeying of running streams by @cHuberCoffee in https://github.com/baresip/baresip/pull/2975
* audio: remove stop_aur() by @cspiel1 in https://github.com/baresip/baresip/pull/2991
* HAVE_INET6 is always defined by @alfredh in https://github.com/baresip/baresip/pull/2992
* audio: respect SDP media disabled flag by @cspiel1 in https://github.com/baresip/baresip/pull/2997
* test: fix test_message() by @alfredh in https://github.com/baresip/baresip/pull/2995
* avformat: do not use deprecated avcodec_close() by @cspiel1 in https://github.com/baresip/baresip/pull/3002
* ua: enforce magic cookie in Via branch by @maximilianfridrich in https://github.com/baresip/baresip/pull/3003
* uag: fix initializer by @cspiel1 in https://github.com/baresip/baresip/pull/3001
* misc: cppcheck fixes by @alfredh in https://github.com/baresip/baresip/pull/3007
* readme: fix lint status badge by @sreimers in https://github.com/baresip/baresip/pull/3008
* video: enable/disable stream at common point by @cspiel1 in https://github.com/baresip/baresip/pull/3010
* srtp: deactivate test_call_srtp_tx_rekey by @cHuberCoffee in https://github.com/baresip/baresip/pull/3013
* g722,g726: use SYSTEM spandsp include by @sreimers in https://github.com/baresip/baresip/pull/3017
* srtp: lock possible re-keying against usage in receive handler by @cHuberCoffee in https://github.com/baresip/baresip/pull/3012
* mc: move multicast to baresip-apps by @cspiel1 in https://github.com/baresip/baresip/pull/3015
* call,audio: remove audio start/stop redundancy by @cspiel1 in https://github.com/baresip/baresip/pull/2999
* aufile: use correct audio format S16LE for aubuf frames by @alfredh in https://github.com/baresip/baresip/pull/3020
* ci: bump pr dependency action by @sreimers in https://github.com/baresip/baresip/pull/3023
* docs,core: remove reference to multicast by @alfredh in https://github.com/baresip/baresip/pull/3019
* bump version by @alfredh in https://github.com/baresip/baresip/pull/3027


**Full Changelog**: https://github.com/baresip/baresip/compare/v3.11.0...v3.12.0


## 3.11.0 - 2024-04-09

### What's Changed
* account: read catchall flag from accounts file by @cspiel1 in https://github.com/baresip/baresip/pull/2925
* vp8/encode: optimizations and target_bitrate fix by @sreimers in https://github.com/baresip/baresip/pull/2936
* vp8,vp9: fix deprecated decode codec init by @sreimers in https://github.com/baresip/baresip/pull/2952
* aureceiver: fix mtx_unlock on discard by @sreimers in https://github.com/baresip/baresip/pull/2955
* release v3.10.1 by @sreimers in https://github.com/baresip/baresip/pull/2958
* message: return 403 instead of 488 by @maximilianfridrich in https://github.com/baresip/baresip/pull/2953
* netroam/cmake: add optional netlink detection by @sreimers in https://github.com/baresip/baresip/pull/2960
* ci/sanitizers: add mmap rnd_bits workaround by @sreimers in https://github.com/baresip/baresip/pull/2967
* account: set inreq_allowed=yes as default by @maximilianfridrich in https://github.com/baresip/baresip/pull/2961
* account: use correct format %zu for printing outbound by @maximilianfridrich in https://github.com/baresip/baresip/pull/2963
* stream: fix empty rtcp_stats for rtx.ssrc reception reports by @sreimers in https://github.com/baresip/baresip/pull/2969
* stream: avoid sanitizer warnings for strm->tx by @cspiel1 in https://github.com/baresip/baresip/pull/2949
* avcodec: remove re_h264 extra header by @sreimers in https://github.com/baresip/baresip/pull/2971
* play: err handling and ensure eof by @cspiel1 in https://github.com/baresip/baresip/pull/2972
* stream: add stream_jbuf_stats()  by @sreimers in https://github.com/baresip/baresip/pull/2973
* sndfile: write correct sample rate to WAV header by @cspiel1 in https://github.com/baresip/baresip/pull/2976
* tls: add session resumption setter by @maximilianfridrich in https://github.com/baresip/baresip/pull/2977
* avcodec: use util function to decode H.264 STAP-A by @alfredh in https://github.com/baresip/baresip/pull/2978
* mixausrc: fix ausrc resampling by @cspiel1 in https://github.com/baresip/baresip/pull/2981
* ci/build: remove obsolete for loop by @cspiel1 in https://github.com/baresip/baresip/pull/2985


**Full Changelog**: https://github.com/baresip/baresip/compare/v3.10.1...v3.11.0


## 3.10.1 - 2024-03-12

* aureceiver: security fix mtx_unlock on discard by @sreimers in https://github.com/baresip/baresip/pull/2955


## 3.10.0 - 2024-03-06

## What's Changed
* cmake: use default value for CMAKE_C_EXTENSIONS by @sreimers in https://github.com/baresip/baresip/pull/2893
* cmake: add /usr/{local,}/include/re and /usr/{local,}/lib{64,} to FindRE.cmake by @robert-scheck in https://github.com/baresip/baresip/pull/2900
* test/main: fix NULL pointer arg on err by @sreimers in https://github.com/baresip/baresip/pull/2902
* ci: add Fedora workflow to avoid e.g. rpath issues by @robert-scheck in https://github.com/baresip/baresip/pull/2904
* mediatrack/start: add audio_decoder_set by @sreimers in https://github.com/baresip/baresip/pull/2910
* config: support distribution-specific/default CA paths by @robert-scheck in https://github.com/baresip/baresip/pull/2905
* readme: cosmetic changes by @robert-scheck in https://github.com/baresip/baresip/pull/2911
* ci/fedora: fix dependency by @sreimers in https://github.com/baresip/baresip/pull/2912
* config: add default CA path for Android by @robert-scheck in https://github.com/baresip/baresip/pull/2913
* transp,tls: add TLS client verification by @maximilianfridrich in https://github.com/baresip/baresip/pull/2888
* account,message,ua: secure incoming SIP MESSAGEs by @maximilianfridrich in https://github.com/baresip/baresip/pull/2877
* aufile: avoid race condition in case of fast destruction by @cspiel1 in https://github.com/baresip/baresip/pull/2917
* aufile: join thread if write fails by @cspiel1 in https://github.com/baresip/baresip/pull/2922
* video: add video_req_keyframe api by @sreimers in https://github.com/baresip/baresip/pull/2920
* call: start streams in sipsess_estab_handler by @maximilianfridrich in https://github.com/baresip/baresip/pull/2909
* webrtc: add av1 codec by @alfredh in https://github.com/baresip/baresip/pull/2916
* cmake: fix relative source dir find paths by @juha-h in https://github.com/baresip/baresip/pull/2924
* echo: fix re_snprintf pointer ARG by @sreimers in https://github.com/baresip/baresip/pull/2927
* cmake: Add include PATH so that GST is found also on Debian 11 by @juha-h in https://github.com/baresip/baresip/pull/2928
* call: improve glare handling by @maximilianfridrich in https://github.com/baresip/baresip/pull/2929
* call: set estdir in call_set_media_direction by @maximilianfridrich in https://github.com/baresip/baresip/pull/2940
* audio,aur: start audio player after early-video by @cspiel1 in https://github.com/baresip/baresip/pull/2941
* ctrl_dbus: add busctl example to module documentation by @maximilianfridrich in https://github.com/baresip/baresip/pull/2944


**Full Changelog**: https://github.com/baresip/baresip/compare/v3.9.0...v3.10.0


## 3.9.0 - 2024-01-31

## What's Changed
* menu autoanswer handling by @cspiel1 in https://github.com/baresip/baresip/pull/2832
* aureceiver: fix overflow multiplications by @sreimers in https://github.com/baresip/baresip/pull/2851
* aur: entirely use mbuf in aurecv_debug() by @cspiel1 in https://github.com/baresip/baresip/pull/2852
* test: call - add AUDIO_MODE_THREAD to test_call_aufilt by @cspiel1 in https://github.com/baresip/baresip/pull/2853
* cmake: add only non-system link paths to rpath (fixes #2849) by @robert-scheck in https://github.com/baresip/baresip/pull/2850
* Renamed gzrtp ARRAY_SIZE macro by @juha-h in https://github.com/baresip/baresip/pull/2855
* avcapture: fix deprecated AVCaptureDeviceTypeExternalUnknown by @sreimers in https://github.com/baresip/baresip/pull/2854
* aur: fix uninitialized warning in auplay handler by @cspiel1 in https://github.com/baresip/baresip/pull/2857
* magic: use assert() instead of BREAKPOINT by @alfredh in https://github.com/baresip/baresip/pull/2847
* sipsess: refactor and simplify SDP negotiation state by @maximilianfridrich in https://github.com/baresip/baresip/pull/2818
* aur: set audio format correctly by @cspiel1 in https://github.com/baresip/baresip/pull/2859
* misc: bump year by @sreimers in https://github.com/baresip/baresip/pull/2860
* video: use viddec_packet by @sreimers in https://github.com/baresip/baresip/pull/2861
* audio: solve concurrency failures of TX thread by @cspiel1 in https://github.com/baresip/baresip/pull/2862
* aur: a mutex for aubuf allocation by @cspiel1 in https://github.com/baresip/baresip/pull/2867
* call: remove unused error handling of some API functions by @cspiel1 in https://github.com/baresip/baresip/pull/2870
* menu: an incoming call should not change the current call by @cspiel1 in https://github.com/baresip/baresip/pull/2869
* misc: HAVE_INET6 is always defined by @alfredh in https://github.com/baresip/baresip/pull/2872
* mqtt: improve disconnect reconnect handling by @sreimers in https://github.com/baresip/baresip/pull/2866
* misc: rx thread activate by @cspiel1 in https://github.com/baresip/baresip/pull/2828
* cmake: refactor module detection by @sreimers in https://github.com/baresip/baresip/pull/2875
* ua: improve SIP 404 warning by @cspiel1 in https://github.com/baresip/baresip/pull/2871
* call: fix race condition for call_set_video_dir() by @cspiel1 in https://github.com/baresip/baresip/pull/2876
* message: allow SIP MESSAGE with application/json ctype by @maximilianfridrich in https://github.com/baresip/baresip/pull/2878
* account/debug: fix wrong and redundant rtcp_mux printf by @sreimers in https://github.com/baresip/baresip/pull/2892
* mk: bump version to 3.9.0 by @alfredh in https://github.com/baresip/baresip/pull/2894


**Full Changelog**: https://github.com/baresip/baresip/compare/v3.8.0...v3.9.0


## 3.8.1 - 2024-01-02

## What's Changed

* aur: set audio format correctly (#2859)
* cmake: add only non-system link paths to rpath (fixes #2849) (#2850)

**Full Changelog**: https://github.com/baresip/baresip/compare/v3.8.0...v3.8.1


## 3.8.0 - 2023-12-27

## What's Changed
* uag: fallback for registrar-less NAT setups by @cspiel1 in https://github.com/baresip/baresip/pull/2810
* menu: fix outgoing early media limit by @cspiel1 in https://github.com/baresip/baresip/pull/2817
* menu: add follow up invite timer by @cspiel1 in https://github.com/baresip/baresip/pull/2812
* rx thread: refactoring part 2 - separate audio receiver by @cspiel1 in https://github.com/baresip/baresip/pull/2795
* pulse: log underruns/overruns after stream terminated by @cspiel1 in https://github.com/baresip/baresip/pull/2820
* call: add call_transp getter by @maximilianfridrich in https://github.com/baresip/baresip/pull/2821
* Decode url in custom header by @nltd101 in https://github.com/baresip/baresip/pull/2816
* video: fix thread sanitizer warning by @cspiel1 in https://github.com/baresip/baresip/pull/2826
* call/rtprecv: fix doxygen comments by @alfredh in https://github.com/baresip/baresip/pull/2825
* uag: use catchall flag for fallback UA selection by @cspiel1 in https://github.com/baresip/baresip/pull/2827
* audio,aur: audio_set_player() only stops auplay by @cspiel1 in https://github.com/baresip/baresip/pull/2830
* cmake: Fix rpath on MacOS by @larsimmisch in https://github.com/baresip/baresip/pull/2831
* cmake/plc: use system include to hide third party warnings by @sreimers in https://github.com/baresip/baresip/pull/2836
* cmake: add link paths to rpath by @sreimers in https://github.com/baresip/baresip/pull/2839
* cmake: bump minimum to 3.14 by @alfredh in https://github.com/baresip/baresip/pull/2838
* readme: update supported OpenSSL/LibreSSL versions by @robert-scheck in https://github.com/baresip/baresip/pull/2843
* cmake: add RE_LIBS config and add atomic check by @sreimers in https://github.com/baresip/baresip/pull/2834
* readme: update supported compiler versions by @robert-scheck in https://github.com/baresip/baresip/pull/2844
* ci: use actions/checkout@v4 by @robert-scheck in https://github.com/baresip/baresip/pull/2845

## New Contributors
* @nltd101 made their first contribution in https://github.com/baresip/baresip/pull/2816

**Full Changelog**: https://github.com/baresip/baresip/compare/v3.7.0...v3.8.0


## 3.7.0 - 2023-11-22

## What's Changed
* Add UA_EVENT_END_OF_FILE  by @larsimmisch in https://github.com/baresip/baresip/pull/2755
* test: call - add test_call_100rel_video by @cspiel1 in https://github.com/baresip/baresip/pull/2762
* call: delay for the initial re-invite after call established by @cspiel1 in https://github.com/baresip/baresip/pull/2764
* Implement OPTIONS ping by @maximilianfridrich in https://github.com/baresip/baresip/pull/2765
* test: call - improve tests for call progress by @cspiel1 in https://github.com/baresip/baresip/pull/2770
* call,event: add CALL_HOLD and CALL_RESUME events and fix call resume requests by @cspiel1 in https://github.com/baresip/baresip/pull/2771
* stream: extract thread safe RTP receiver by @cspiel1 in https://github.com/baresip/baresip/pull/2685
* test: call - count audio frames by @cspiel1 in https://github.com/baresip/baresip/pull/2776
* main: add pre-proc switch avoids warning by @cspiel1 in https://github.com/baresip/baresip/pull/2778
* rtprecv: fix possible rtprecv_metric null pointer deref by @juha-h in https://github.com/baresip/baresip/pull/2786
* rtprecv: add NULL pointer checks by @cspiel1 in https://github.com/baresip/baresip/pull/2787
* test: call - add call on-hold/resume test by @cspiel1 in https://github.com/baresip/baresip/pull/2775
* test: activate RTP stats by @cspiel1 in https://github.com/baresip/baresip/pull/2789
* test: call - more stable test_call_change_videodir by @cspiel1 in https://github.com/baresip/baresip/pull/2790
* test: call - add 100rel test for audio by @cspiel1 in https://github.com/baresip/baresip/pull/2779
* test: call - wait for ACK after SDP answer by @cspiel1 in https://github.com/baresip/baresip/pull/2792
* test: call - remove unstable check by @cspiel1 in https://github.com/baresip/baresip/pull/2794
* Debian version upgrade by @juha-h in https://github.com/baresip/baresip/pull/2796
* cmake/modules: exclude ctrl_dbus from Darwin/macOS by @sreimers in https://github.com/baresip/baresip/pull/2798
* ci: use macos-latest by @alfredh in https://github.com/baresip/baresip/pull/2799
* config: fix/split config_print arg lengths by @sreimers in https://github.com/baresip/baresip/pull/2801
* test/call.c: extend test_call_hold_resume by @maximilianfridrich in https://github.com/baresip/baresip/pull/2800
* rtpstat: fix stream_metric_get_rx_n_err stream arg by @sreimers in https://github.com/baresip/baresip/pull/2803
* test/ua: fix reg_dns size_t format by @sreimers in https://github.com/baresip/baresip/pull/2804
* test/main: fix unused i if HAVE_GETOPT is not available by @sreimers in https://github.com/baresip/baresip/pull/2805
* audio: fix inbound dtmf END event by @cspiel1 in https://github.com/baresip/baresip/pull/2802
* video: check vidcodec argument in video_decoder_set() by @alfredh in https://github.com/baresip/baresip/pull/2806
* gtk: close GTK on unsupported icon by @mbattista in https://github.com/baresip/baresip/pull/2808
* stream: lock tx.pt_enc fixes sanitizer warning by @cspiel1 in https://github.com/baresip/baresip/pull/2809
* avcodec: fix FFmpeg 6.1 AVframe key_frame deprecation by @sreimers in https://github.com/baresip/baresip/pull/2807
* jbuf: fix memory leak in jbuf_debug() by @cspiel1 in https://github.com/baresip/baresip/pull/2813
* jbuf: add NULL pointer check for mbuf by @cspiel1 in https://github.com/baresip/baresip/pull/2814

## New Contributors
* @larsimmisch made their first contribution in https://github.com/baresip/baresip/pull/2755

**Full Changelog**: https://github.com/baresip/baresip/compare/v3.6.0...v3.7.0


## 3.6.0 - 2023-10-17

## What's Changed
* test: call - replace stop_on_audio_video by cancel rule by @cspiel1 in https://github.com/baresip/baresip/pull/2701
* video: use const struct video for videnc_update_h and viddec_update_h by @sreimers in https://github.com/baresip/baresip/pull/2670
* misc: fd_listen fhs alloc rewrite by @sreimers in https://github.com/baresip/baresip/pull/2688
* ctrl_tcp: fix netstring enum warning by @sreimers in https://github.com/baresip/baresip/pull/2730
* ua, static_menu: Fix 100rel cmd by @maximilianfridrich in https://github.com/baresip/baresip/pull/2731
* tools: jbuf plots by @cspiel1 in https://github.com/baresip/baresip/pull/2733
* tools: fix and cleanup ajb plots by @cspiel1 in https://github.com/baresip/baresip/pull/2736
* ua: move adding of norefersub extension to create_register_clients by @maximilianfridrich in https://github.com/baresip/baresip/pull/2734
* main: add re_trace.json if enabled by @sreimers in https://github.com/baresip/baresip/pull/2738
* jbuf: move from re to baresip by @cspiel1 in https://github.com/baresip/baresip/pull/2743
* avcodec/decode: refactor hw_frame handling by @sreimers in https://github.com/baresip/baresip/pull/2720
* call: include Referred-by: tag in REFERs by @rodrigodeppe in https://github.com/baresip/baresip/pull/2739
* ci: bump pr-dependency-action@v0.6 by @sreimers in https://github.com/baresip/baresip/pull/2746
* video: add video decode error trace by @sreimers in https://github.com/baresip/baresip/pull/2748
* video: protect shared resources in video_debug by @paresy in https://github.com/baresip/baresip/pull/2747
* video: delay video_destructor by @sreimers in https://github.com/baresip/baresip/pull/2751
* avcodec/decode: revert hw_frame handling and fix unref frame by @sreimers in https://github.com/baresip/baresip/pull/2752
* avcodec/decode: fix last av_frame memory leak by @sreimers in https://github.com/baresip/baresip/pull/2753
* test: call - count video frames in videodir tests by @cspiel1 in https://github.com/baresip/baresip/pull/2758
* test call - fix logical and by @cspiel1 in https://github.com/baresip/baresip/pull/2763

## New Contributors
* @rodrigodeppe made their first contribution in https://github.com/baresip/baresip/pull/2739
* @paresy made their first contribution in https://github.com/baresip/baresip/pull/2747

**Full Changelog**: https://github.com/baresip/baresip/compare/v3.5.1...v3.6.0

## 3.5.1 - 2023-09-12

## What's Changed
* cmake: fix RE_DEFINITIONS by @sreimers in https://github.com/baresip/baresip/pull/2716

**Full Changelog**: https://github.com/baresip/baresip/compare/v3.5.0...v3.5.1


## 3.5.0 - 2023-09-12

## What's Changed
* mc: fix format string by @cspiel1 in https://github.com/baresip/baresip/pull/2675
* call: never set sent_answer to false by @cspiel1 in https://github.com/baresip/baresip/pull/2674
* video: add source and display name getters by @sreimers in https://github.com/baresip/baresip/pull/2669
* cmake: fix clang gnu-zero-variadic-macro-arguments warning by @sreimers in https://github.com/baresip/baresip/pull/2677
* test call cancel rules by @cspiel1 in https://github.com/baresip/baresip/pull/2667
* stream: declare ext_len when assigned by @alfredh in https://github.com/baresip/baresip/pull/2678
* ci/mingw: remove cmake workaround by @sreimers in https://github.com/baresip/baresip/pull/2679
* call: fix Refer-To URI angle brackets by @sreimers in https://github.com/baresip/baresip/pull/2681
* test: call - replace re_cancel in CALL_RTCP and REMOTE_SDP by rule by @cspiel1 in https://github.com/baresip/baresip/pull/2684
* test: add rtcp_mux test by @cspiel1 in https://github.com/baresip/baresip/pull/2692
* test: remove unused local variable in test_call_bundle_base() by @cspiel1 in https://github.com/baresip/baresip/pull/2693
* account,docs: cleanup for accounts config by @cspiel1 in https://github.com/baresip/baresip/pull/2691
* test: call - combine cancel rules with logical AND by @cspiel1 in https://github.com/baresip/baresip/pull/2687
* ccheck: add PRI*64 check (use %L instead) by @sreimers in https://github.com/baresip/baresip/pull/2695
* test: call - replace stop_on_rtp by cancel rules by @cspiel1 in https://github.com/baresip/baresip/pull/2697
* webrtc/js: add rtcpMuxPolicy require policy by @sreimers in https://github.com/baresip/baresip/pull/2699
* gst: gst_deinit() should be last gst call by @cspiel1 in https://github.com/baresip/baresip/pull/2703


**Full Changelog**: https://github.com/baresip/baresip/compare/v3.4.0...v3.5.0


## 3.4.0 - 2023-08-09

## What's Changed
* tools: add adaptive aubuf plot generation by @cspiel1 in https://github.com/baresip/baresip/pull/2641
* webrtc: add media track sdp direction by @sreimers in https://github.com/baresip/baresip/pull/2636
* webrtc: remove G711 module by @alfredh in https://github.com/baresip/baresip/pull/2643
* cmake: fix default path in FindAMR.cmake by @cspiel1 in https://github.com/baresip/baresip/pull/2644
* cmake: FindPNG.cmake - correct else path for PNG_FOUND by @cspiel1 in https://github.com/baresip/baresip/pull/2645
* webrtc: fix format and minor improvements by @alfredh in https://github.com/baresip/baresip/pull/2647
* cmake: add /usr/lib{64,}/glib-2.0 to FindGST.cmake by @robert-scheck in https://github.com/baresip/baresip/pull/2648
* ci/mingw: downgrade cmake by @sreimers in https://github.com/baresip/baresip/pull/2656
* call: logic fix in call_modify. remove call->sent_answer = false by @cHuberCoffee in https://github.com/baresip/baresip/pull/2655
* cmake/modules: fix ffmpeg static linking order by @sreimers in https://github.com/baresip/baresip/pull/2659
* webrtc: use 640x480 resolution for both sending and receiving by @alfredh in https://github.com/baresip/baresip/pull/2658
* readme: update list of RFCs by @alfredh in https://github.com/baresip/baresip/pull/2657
* test/cmake: list source files in alphabetical order by @alfredh in https://github.com/baresip/baresip/pull/2661
* test: fix typo in call test by @cspiel1 in https://github.com/baresip/baresip/pull/2666
* video,stream: stop natpinhole timer on re-invite by @cspiel1 in https://github.com/baresip/baresip/pull/2665


**Full Changelog**: https://github.com/baresip/baresip/compare/v3.3.0...v3.4.0


## 3.3.0 - 2023-07-05

## What's Changed
* Removed unused zrtp_hash config setting by @juha-h in https://github.com/baresip/baresip/pull/2591
* audio: remove obsolete aurx fields by @cspiel1 in https://github.com/baresip/baresip/pull/2590
* mixausrc: sweep fading for performance by @cHuberCoffee in https://github.com/baresip/baresip/pull/2605
* config: clean up config template by @alfredh in https://github.com/baresip/baresip/pull/2609
* Handle UA_EVENT_CALL_REDIRECT (formerly UA_EVENT_CALL_BLIND_TRANSFER)~ by @juha-h in https://github.com/baresip/baresip/pull/2602
* ci/mingw: use cv2pdb for debug info conversion by @sreimers in https://github.com/baresip/baresip/pull/2610
* config: some refactoring by @alfredh in https://github.com/baresip/baresip/pull/2611
* config: missing updates for config example and template by @cspiel1 in https://github.com/baresip/baresip/pull/2613
* Fix oneway video sdp on answering call by @maximilianfridrich in https://github.com/baresip/baresip/pull/2615
* call,aucodec: add aucodec_print to internal core API by @cspiel1 in https://github.com/baresip/baresip/pull/2616
* account,stream: natpinhole account parameter orthogonal to medianat by @cspiel1 in https://github.com/baresip/baresip/pull/2618
* docs: update default value for natpinhole by @cspiel1 in https://github.com/baresip/baresip/pull/2619
* stream: increase udp socket buffer for video by @sreimers in https://github.com/baresip/baresip/pull/2620
* stream: faster video jitter buffer offloading by @sreimers in https://github.com/baresip/baresip/pull/2622
* config: add default_audio_path() by @alfredh in https://github.com/baresip/baresip/pull/2621
* event: correct class name for SDP events by @cspiel1 in https://github.com/baresip/baresip/pull/2625
* call: send LOCAL_SDP event if we send SDP by @cspiel1 in https://github.com/baresip/baresip/pull/2626
* config: refactor sip_cafile to make the code more clean by @alfredh in https://github.com/baresip/baresip/pull/2627
* audio: rework on codec changes by @cspiel1 in https://github.com/baresip/baresip/pull/2630
* event: add local SDP direction by @cspiel1 in https://github.com/baresip/baresip/pull/2629
* account: set sip_auroredirect default to no by @cspiel1 in https://github.com/baresip/baresip/pull/2632
* stream: also decode audio as long as jbuf returns EAGAIN by @cspiel1 in https://github.com/baresip/baresip/pull/2635
* aufile: init sampv buffer; NULL pointer check by @cspiel1 in https://github.com/baresip/baresip/pull/2637


**Full Changelog**: https://github.com/baresip/baresip/compare/v3.2.0...v3.3.0



## 3.2.0 - 2023-05-31

## What's Changed
* ci: add coverage workflow by @maximilianfridrich in https://github.com/baresip/baresip/pull/2550
* cmake: fix win32 dbghelp by @sreimers in https://github.com/baresip/baresip/pull/2552
* cmake/gtk3: make sure gtk3 libs are found by @landryb in https://github.com/baresip/baresip/pull/2554
* pipewire: add pipewire module by @cspiel1 in https://github.com/baresip/baresip/pull/2439
* audio: count TX underruns correctly (#2535) by @cspiel1 in https://github.com/baresip/baresip/pull/2553
* variadic function fixes by @maximilianfridrich in https://github.com/baresip/baresip/pull/2523
* ua: unescape incoming Refer-To header by @maximilianfridrich in https://github.com/baresip/baresip/pull/2541
* pipewire/cmake: declare include dirs as system by @sreimers in https://github.com/baresip/baresip/pull/2556
* sndio: re-add sndio module for OpenBSD by @landryb in https://github.com/baresip/baresip/pull/2555
* Client cert renegotiation in http by @fAuernigg in https://github.com/baresip/baresip/pull/2461
* aufile: joind already terminated thread frees stack by @cspiel1 in https://github.com/baresip/baresip/pull/2557
* ccheck: c11 err handling exclude mutex\_alloc by @sreimers in https://github.com/baresip/baresip/pull/2560
* alsa: use atomic for run flag by @cspiel1 in https://github.com/baresip/baresip/pull/2558
* Add instruction to build the doxygen docs by @gibix in https://github.com/baresip/baresip/pull/2561
* fakevideo: use atomic for run flag by @cspiel1 in https://github.com/baresip/baresip/pull/2565
* cmake/findOpus: fix Could NOT find OPUS (missing: OPUS\_INCLUDE\_DIR) by @jobo-zt in https://github.com/baresip/baresip/pull/2568
* cmake/findsdl: fix Could NOT find SDL (missing: SDL\_INCLUDE\_DIR) by @jobo-zt in https://github.com/baresip/baresip/pull/2571
* avformat, g722: add macro UNISTD switch support for Windows by @jobo-zt in https://github.com/baresip/baresip/pull/2574
* v4l2: use atomic for run flag by @cspiel1 in https://github.com/baresip/baresip/pull/2567
* mc: use atomic for run flag by @cspiel1 in https://github.com/baresip/baresip/pull/2566
* avformat: use atomic for run flag by @cspiel1 in https://github.com/baresip/baresip/pull/2564
* ausine: use atomic for run flag by @cspiel1 in https://github.com/baresip/baresip/pull/2563
* aubridge: use atomic for run flag by @cspiel1 in https://github.com/baresip/baresip/pull/2562
* config: add different options for audio and video jitter buffer by @sreimers in https://github.com/baresip/baresip/pull/2569
* Revert "ua: unescape incoming Refer-To header (#2541)" by @maximilianfridrich in https://github.com/baresip/baresip/pull/2577
* stream: log last RTP packet debug after 100ms by @sreimers in https://github.com/baresip/baresip/pull/2587
* ci/sanitizers: exit on first undefined behavior by @sreimers in https://github.com/baresip/baresip/pull/2589

## New Contributors
* @landryb made their first contribution in https://github.com/baresip/baresip/pull/2554
* @gibix made their first contribution in https://github.com/baresip/baresip/pull/2561
* @jobo-zt made their first contribution in https://github.com/baresip/baresip/pull/2568

**Full Changelog**: https://github.com/baresip/baresip/compare/v3.1.0...v3.2.0

## 3.1.0 - 2023-04-27

## What's Changed
* config: add net_af config setting by @juha-h in https://github.com/baresip/baresip/pull/2490
* gzrtp: RX thread - safe stop by @cspiel1 in https://github.com/baresip/baresip/pull/2492
* ci: avoid hardcoded OpenSSL path on macOS by @robert-scheck in https://github.com/baresip/baresip/pull/2505
* fix cmake modules by @sreimers in https://github.com/baresip/baresip/pull/2507
* cmake/mqtt: fix MOSQUITTO_LIBRARY by @sreimers in https://github.com/baresip/baresip/pull/2508
* mc: send module event whenever receiver is stopped by @cspiel1 in https://github.com/baresip/baresip/pull/2509
* menu: limit early audio TX streams by @cspiel1 in https://github.com/baresip/baresip/pull/2503
* call: check if SIP UPDATE is allowed, but always update local media by @cspiel1 in https://github.com/baresip/baresip/pull/2504
* account: increase line handler size to 1024 characters by @juha-h in https://github.com/baresip/baresip/pull/2511
* cmake: avoid include of /usr/local/include by @cspiel1 in https://github.com/baresip/baresip/pull/2506
* call,audio: respect SDP media dir on audio start similar to video by @cspiel1 in https://github.com/baresip/baresip/pull/2501
* video: refactor paced and burst sending by @sreimers in https://github.com/baresip/baresip/pull/2482
* ctrl_dbus,ice,png_vf: Fix format string usage by @maximilianfridrich in https://github.com/baresip/baresip/pull/2517
* menu limit early video by @cspiel1 in https://github.com/baresip/baresip/pull/2514
* play: flush of the aubuf directly before the replay starts by @cspiel1 in https://github.com/baresip/baresip/pull/2512
* stream: fix setting of RTP tos for IPv6 by @cspiel1 in https://github.com/baresip/baresip/pull/2527
* call: only flush audio stream when stream starts by @cspiel1 in https://github.com/baresip/baresip/pull/2526
* menu: use busy tone when call declined (scode 603) by @cspiel1 in https://github.com/baresip/baresip/pull/2529
* ua: incoming DTMF key=0 should be reported as DTMF end by @cspiel1 in https://github.com/baresip/baresip/pull/2528
* video: fix possible 32bit overflow by @sreimers in https://github.com/baresip/baresip/pull/2534
* ua: deref call on reset_transp fail by @maximilianfridrich in https://github.com/baresip/baresip/pull/2532
* uag: avoid transport reset if local address has not changed by @juha-h in https://github.com/baresip/baresip/pull/2537
* ci: add gcc-12 for Ubuntu 22.04 (ubuntu-latest) by @robert-scheck in https://github.com/baresip/baresip/pull/2542
* docs: remove librem from README files by @robert-scheck in https://github.com/baresip/baresip/pull/2543

**Full Changelog**: https://github.com/baresip/baresip/compare/v3.0.0...v3.1.0

---

## 3.0.0 - 2023-03-20

## What's Changed
* ua: allow custom headers in sip REGISTER request by @Koshub in https://github.com/baresip/baresip/pull/2452
* merge rem into re by @alfredh in https://github.com/baresip/baresip/pull/2442
* main: fix async init order (after config load) by @sreimers in https://github.com/baresip/baresip/pull/2457
* ci: install pkg-config on mac-os by @cspiel1 in https://github.com/baresip/baresip/pull/2459
* ci: remove rem in sanitizers and valgrind yml by @cspiel1 in https://github.com/baresip/baresip/pull/2458
* video: fix vidqueue_poll list_move by @sreimers in https://github.com/baresip/baresip/pull/2465
* Dshow fixes by @tomek-o in https://github.com/baresip/baresip/pull/2467
* Moved adding of custom headers from ua_connect_dir to ua_call_alloc by @juha-h in https://github.com/baresip/baresip/pull/2470
* Include also params to MESSAGE URI by @juha-h in https://github.com/baresip/baresip/pull/2469
* video: remove unused qent->dst by @sreimers in https://github.com/baresip/baresip/pull/2474
* call: Fix delayed (auto) answer if awaiting PRACK by @maximilianfridrich in https://github.com/baresip/baresip/pull/2473
* video: add TX thread by @sreimers in https://github.com/baresip/baresip/pull/2460
* ccheck: add check_list_unlink check by @sreimers in https://github.com/baresip/baresip/pull/2471
* stream: add stream_enable_tx() api by @sreimers in https://github.com/baresip/baresip/pull/2479
* audio: align Audio TX thread name by @sreimers in https://github.com/baresip/baresip/pull/2480
* Send event when dump file is opened by @juha-h in https://github.com/baresip/baresip/pull/2486
* video: add NULL pointer check for vidisp by @cspiel1 in https://github.com/baresip/baresip/pull/2483
* ua: Fix calls of ua_event() by @maximilianfridrich in https://github.com/baresip/baresip/pull/2495
* call: Fix calls of call_event_handler by @maximilianfridrich in https://github.com/baresip/baresip/pull/2496

## New Contributors
* @Koshub made their first contribution in https://github.com/baresip/baresip/pull/2452
* @tomek-o made their first contribution in https://github.com/baresip/baresip/pull/2467

**Full Changelog**: https://github.com/baresip/baresip/compare/v2.12.0...v3.0.0

---

## 2.12.0 - 2023-02-15

## What's Changed
* call: default status code for rejecting incoming calls by @cspiel1 in https://github.com/baresip/baresip/pull/2409
* dtls_srtp: enable single DTLS connection mode by @alfredh in https://github.com/baresip/baresip/pull/2411
* ci: try to fix flaky azure mirrors by @sreimers in https://github.com/baresip/baresip/pull/2413
* cmake/pulse: Remove pulse-simple library lookup by @robert-scheck in https://github.com/baresip/baresip/pull/2414
* webrtc_aecm: use C11 mutex by @juha-h in https://github.com/baresip/baresip/pull/2415
* pulse: replace obsolete string pulse_async (makes baresip PipeWire compatible) by @cspiel1 in https://github.com/baresip/baresip/pull/2412
* vidpacket: add keyframe flag by @alfredh in https://github.com/baresip/baresip/pull/2416
* av1: use keyframe instead of new-flag by @alfredh in https://github.com/baresip/baresip/pull/2418
* av1: fix warnings by @alfredh in https://github.com/baresip/baresip/pull/2419
* make rtcp interval configureable by @sreimers in https://github.com/baresip/baresip/pull/2420
* sndio: remove deprecated module by @alfredh in https://github.com/baresip/baresip/pull/2422
* PRACK refactoring by @maximilianfridrich in https://github.com/baresip/baresip/pull/2401
* ci: merge build and cmake by @alfredh in https://github.com/baresip/baresip/pull/2425
* menu: ringback/early audio handling for parallel calls by @cspiel1 in https://github.com/baresip/baresip/pull/2403
* magic: use C99 __func__ macro by @alfredh in https://github.com/baresip/baresip/pull/2427
* stream: remove stream_decode from internal API by @cspiel1 in https://github.com/baresip/baresip/pull/2430
* use RE_ARRAY_SIZE() macro by @alfredh in https://github.com/baresip/baresip/pull/2429
* cmake: link RESOLV_LIBRARY by @sreimers in https://github.com/baresip/baresip/pull/2432
* ci/build: fix Ubuntu 22.04 workaround by @sreimers in https://github.com/baresip/baresip/pull/2435
* avcapture: use RE_ARRAY_SIZE macro by @alfredh in https://github.com/baresip/baresip/pull/2434
* pulse: remove obsolete doxygen note to be experimental by @cspiel1 in https://github.com/baresip/baresip/pull/2436
* gtk: return NULL on mtx_init() != thrd_success by @robert-scheck in https://github.com/baresip/baresip/pull/2440
* ci: add libgtk-3-dev to build GTK+ 3 module by @robert-scheck in https://github.com/baresip/baresip/pull/2441
* event: missing class name case for RTPESTAB event by @cspiel1 in https://github.com/baresip/baresip/pull/2447
* ci: add sanitizers by @sreimers in https://github.com/baresip/baresip/pull/2449
* bump version numbers to 2.12.0 by @alfredh in https://github.com/baresip/baresip/pull/2453


**Full Changelog**: https://github.com/baresip/baresip/compare/v2.11.0...v2.12.0

---

## 2.11.0 - 2023-01-11

### What's Changed
* uag,call: do not override status code and reason by @cspiel1 in https://github.com/baresip/baresip/pull/2345
* stream: send RTP NAT pinhole opener until RTP is received by @cspiel1 in https://github.com/baresip/baresip/pull/2346
* mediatrack: add audio and video getters by @sreimers in https://github.com/baresip/baresip/pull/2347
* Added rtcp_mux related API functions by @juha-h in https://github.com/baresip/baresip/pull/2352
* make: remove deprecated makefile by @alfredh in https://github.com/baresip/baresip/pull/2354
* Removed rtcp_mux config variable by @juha-h in https://github.com/baresip/baresip/pull/2353
* Use bool instead of "yes"/"no" in account API functions by @juha-h in https://github.com/baresip/baresip/pull/2355
* aubuf: add AUBUF_FILE mode by @cspiel1 in https://github.com/baresip/baresip/pull/2363
* play: flush aubuf before restart by @cspiel1 in https://github.com/baresip/baresip/pull/2364
* call: avoid unwanted re-invites on ESTABLISHED event by @cspiel1 in https://github.com/baresip/baresip/pull/2362
* avcodec: constrain bitrate by @sreimers in https://github.com/baresip/baresip/pull/2365
* pulse: rename to pulse_simple.so by @alfredh in https://github.com/baresip/baresip/pull/2371
* module: remove module_tmp by @alfredh in https://github.com/baresip/baresip/pull/2373
* audio: remove unused last_sampc by @alfredh in https://github.com/baresip/baresip/pull/2372
* audio: add rtpext_find() (refactoring) by @alfredh in https://github.com/baresip/baresip/pull/2375
* multicast: remove ref to pthread by @alfredh in https://github.com/baresip/baresip/pull/2379
* video: add rtcp-fb Generic NACK by @sreimers in https://github.com/baresip/baresip/pull/2380
* call: set media dir also for MNAT case by @cspiel1 in https://github.com/baresip/baresip/pull/2382
* pulse: rename pulse_async.so to pulse.so (default) by @alfredh in https://github.com/baresip/baresip/pull/2381
* RTP Resend by @sreimers in https://github.com/baresip/baresip/pull/2378
* make: remove unused srcs.mk by @alfredh in https://github.com/baresip/baresip/pull/2387
* TLS server support SNI based certificate selection by @cspiel1 in https://github.com/baresip/baresip/pull/2330
* audiounit: use C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/2386
* webrtc_aec: use C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/2384
* coreaudio: use C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/2388
* gtk: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2391
* remove pulse_simple.so -- use pulse.so by @alfredh in https://github.com/baresip/baresip/pull/2383
* ci: rename ccheck.yml to lint.yml by @alfredh in https://github.com/baresip/baresip/pull/2394
* fritzbox2baresip: use open with explicitly specifying an encoding by @robert-scheck in https://github.com/baresip/baresip/pull/2396
* test: remove mock_aufilt (unused) by @alfredh in https://github.com/baresip/baresip/pull/2392
* Ci pylint by @alfredh in https://github.com/baresip/baresip/pull/2395
* gzrtp: use C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/2393
* C11 mutex by @mbattista in https://github.com/baresip/baresip/pull/2397
* tls multiple server certs by @cspiel1 in https://github.com/baresip/baresip/pull/2399
* call: return EINVAL if answer not possible by @maximilianfridrich in https://github.com/baresip/baresip/pull/2405
* ccheck: fix some pylint warnings by @alfredh in https://github.com/baresip/baresip/pull/2398
* Fixed account debug of mwi and call_transfer by @juha-h in https://github.com/baresip/baresip/pull/2406
* avformat: fix printf format for samplerate and channels by @alfredh in https://github.com/baresip/baresip/pull/2407
* cmake: increase minimum required version by @cspiel1 in https://github.com/baresip/baresip/pull/2408


**Full Changelog**: https://github.com/baresip/baresip/compare/v2.10.0...v2.11.0

---

## [2.10.0] - 2022-12-06

### What's Changed
* sdl: small improvements by @sreimers in https://github.com/baresip/baresip/pull/2285
* vidinfo: allow all pixel formats by @alfredh in https://github.com/baresip/baresip/pull/2291
* vid: add support for YUV422P pixel format by @alfredh in https://github.com/baresip/baresip/pull/2280
* avformat: fix hwaccel vaapi by @alfredh in https://github.com/baresip/baresip/pull/2299
* mk: add deprecate notice by @alfredh in https://github.com/baresip/baresip/pull/2302
* mingw: upgrade to OpenSSL 3.0.7 by @alfredh in https://github.com/baresip/baresip/pull/2303
* dshow: fix some warnings by @alfredh in https://github.com/baresip/baresip/pull/2305
* dshow: fix pragma warning by @alfredh in https://github.com/baresip/baresip/pull/2306
* ci: install libsdl2 development package by @alfredh in https://github.com/baresip/baresip/pull/2307
* sdl: work in progress fixes for multi-threading by @alfredh in https://github.com/baresip/baresip/pull/2300
* Stop segfaulting when no URI is passed to dial command by @SimonHyde-BBC in https://github.com/baresip/baresip/pull/2311
* ice: local candidate policy config by @sreimers in https://github.com/baresip/baresip/pull/2312
* auresamp: check handler arguments by @alfredh in https://github.com/baresip/baresip/pull/2313
* fixes 2315 and GTK errors on quit by @mbattista in https://github.com/baresip/baresip/pull/2316
* auresamp: avoid division by zero (#2293) by @cspiel1 in https://github.com/baresip/baresip/pull/2317
* cmake: check for XShm.h (#2318) by @cspiel1 in https://github.com/baresip/baresip/pull/2319
* pulse_async: avoid integer overrun for timestamps in recorder by @cspiel1 in https://github.com/baresip/baresip/pull/2321
* ua: use sdp connection data instead origin by @sreimers in https://github.com/baresip/baresip/pull/2298
* rtpext: move from baresip to re by @alfredh in https://github.com/baresip/baresip/pull/2322
* acc,stream: add rtcp_mux account param by @sreimers in https://github.com/baresip/baresip/pull/2320
* video: video_update cleanup by @sreimers in https://github.com/baresip/baresip/pull/2324
* aufile/src: add auframe support by @sreimers in https://github.com/baresip/baresip/pull/2325
* ice/tmr_async_handler: fix possible segfault by @sreimers in https://github.com/baresip/baresip/pull/2326
* webrtc: fix browser offer handling by @sreimers in https://github.com/baresip/baresip/pull/2327
* Space at the beginning of sip: creates errors by @mbattista in https://github.com/baresip/baresip/pull/2329
* opus_multistream: update mimetype to ad-hoc standard by @alfredh in https://github.com/baresip/baresip/pull/2328
* webrtc: add offerer and recvonly options by @sreimers in https://github.com/baresip/baresip/pull/2331
* test: replace RSA cert with EC cert by @alfredh in https://github.com/baresip/baresip/pull/2332
* Add OPTIONS handling for webrtc demo by @RenSym in https://github.com/baresip/baresip/pull/2333
* mk: remove rtpext.c from srcs.mk by @cspiel1 in https://github.com/baresip/baresip/pull/2336
* ua: change refer log to info() by @alfredh in https://github.com/baresip/baresip/pull/2338

### New Contributors
* @SimonHyde-BBC made their first contribution in https://github.com/baresip/baresip/pull/2311
* @RenSym made their first contribution in https://github.com/baresip/baresip/pull/2333

**Full Changelog**: https://github.com/baresip/baresip/compare/v2.9.0...v2.10.0

---

## [2.9.0] - 2022-11-01

### What's Changed
* sndfile Module - filename includes strm->cname (i.e. call->local_uri)~ by @ninp0 in https://github.com/baresip/baresip/pull/2165
* log: optional timestamps by @cspiel1 in https://github.com/baresip/baresip/pull/2169
* avcodec: remove H263 codec by @alfredh in https://github.com/baresip/baresip/pull/2182
* mk: bump PROJECT_NUMBER in Doxyfile by @cspiel1 in https://github.com/baresip/baresip/pull/2201
* stream: correct Doxygen for peer field by @cspiel1 in https://github.com/baresip/baresip/pull/2202
* cmake: add pre version handling by @sreimers in https://github.com/baresip/baresip/pull/2203
* cmake,debian: use dh-cmake by @sreimers in https://github.com/baresip/baresip/pull/2204
* cmake: add pkgconfig by @robert-scheck in https://github.com/baresip/baresip/pull/2205
* Avoid webrtc_aecm module C++20 extension warnings by @juha-h in https://github.com/baresip/baresip/pull/2215
* cmake/ctrld_dbus: ninja and subdirectory fixes by @sreimers in https://github.com/baresip/baresip/pull/2221
* cmake: link CMAKE_CURRENT_BINARY_DIR modules by @sreimers in https://github.com/baresip/baresip/pull/2223
* cmake,debian: fix libbaresip dependency by @sreimers in https://github.com/baresip/baresip/pull/2224
* cmake: set C only flags by @sreimers in https://github.com/baresip/baresip/pull/2226
* FindPNG needs to find also include directory by @juha-h in https://github.com/baresip/baresip/pull/2230
* FindVPX needs to find also include directory by @juha-h in https://github.com/baresip/baresip/pull/2231
* Multicast send events on mcreg enable commands by @cHuberCoffee in https://github.com/baresip/baresip/pull/2219
* call, menu: support display name for outgoing calls by @cspiel1 in https://github.com/baresip/baresip/pull/2220
* call: hangup call on transp reset if necessary by @maximilianfridrich in https://github.com/baresip/baresip/pull/2229
* portaudio: add mediadev_add with mediadev driver fields by @sreimers in https://github.com/baresip/baresip/pull/2233
* call: fix mnat call_streams_alloc by @sreimers in https://github.com/baresip/baresip/pull/2242
* jack: fix CodeQL uninitialized local variable by @sreimers in https://github.com/baresip/baresip/pull/2244
* Avoid snapshot compiler warnings by @juha-h in https://github.com/baresip/baresip/pull/2239
* avformat: remove old call to avcodec_register_all() by @alfredh in https://github.com/baresip/baresip/pull/2246
* avformat: remove LIBAVUTIL_VERSION_MAJOR check by @alfredh in https://github.com/baresip/baresip/pull/2247
* ua: wording for warning in ua_refer_send() by @cspiel1 in https://github.com/baresip/baresip/pull/2249
* ua: use mbuf functions for ua_connect_dir by @cspiel1 in https://github.com/baresip/baresip/pull/2250
* ci: use actions/checkout@v3 by @sreimers in https://github.com/baresip/baresip/pull/2254
* avcodec: remove av_packet_free() wrapper by @alfredh in https://github.com/baresip/baresip/pull/2255
* selfview: create window in encode_update by @alfredh in https://github.com/baresip/baresip/pull/2253
* alsa: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2256
* config: fix template for avcodec_xxx by @alfredh in https://github.com/baresip/baresip/pull/2258
* avformat: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2259
* v4l2: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2261
* avcodec: remove LIBAVUTIL_VERSION_MAJOR check by @alfredh in https://github.com/baresip/baresip/pull/2260
* multicast: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2262
* menu fix display name by @cspiel1 in https://github.com/baresip/baresip/pull/2251
* account: do not complete dial URI if scheme is included by @cspiel1 in https://github.com/baresip/baresip/pull/2267
* menu: simplify URI complete by @cspiel1 in https://github.com/baresip/baresip/pull/2268
* gtk: use new function account_uri_complete_strdup() by @cspiel1 in https://github.com/baresip/baresip/pull/2273
* Removed module avformat dependency on libpostproc by @juha-h in https://github.com/baresip/baresip/pull/2274
* make: detect and add swscale module in modules.mk by @agorgl in https://github.com/baresip/baresip/pull/2281
* cmake: add APP_MODULES symlinks by @sreimers in https://github.com/baresip/baresip/pull/2286
* cmake: use CMAKE_SHARED_MODULE_SUFFIX by @sreimers in https://github.com/baresip/baresip/pull/2292

## New Contributors
* @ninp0 made their first contribution in https://github.com/baresip/baresip/pull/2165
* @agorgl made their first contribution in https://github.com/baresip/baresip/pull/2281

**Full Changelog**: https://github.com/baresip/baresip/compare/v2.8.1...v2.9.0


## [2.8.1] - 2022-10-01

* baresip.h: bump BARESIP\_VERSION by @cspiel1 in https://github.com/baresip/baresip/pull/2196

## [2.8.0] - 2022-10-01

* opensles cmake by @juha-h in https://github.com/baresip/baresip/pull/2108
* test/call: Add test\_call\_change\_videodir by @maximilianfridrich in https://github.com/baresip/baresip/pull/2080
* cmake: bump min version to 3.10 by @alfredh in https://github.com/baresip/baresip/pull/2112
* zrtp: remove module, use gzrtp instead by @alfredh in https://github.com/baresip/baresip/pull/2109
* Avoid gzrtp compile warnings by @juha-h in https://github.com/baresip/baresip/pull/2110
* Update video in menu when UA\_EVENT\_CALL\_REMOTE\_SDP is recieved by @juha-h in https://github.com/baresip/baresip/pull/2113
* http/https requests with large body by @fAuernigg in https://github.com/baresip/baresip/pull/2100
* call: send reinvite after established handlers by @maximilianfridrich in https://github.com/baresip/baresip/pull/2117
* refer out of dialog by @cspiel1 in https://github.com/baresip/baresip/pull/2115
* remove unused functions in baresip.h by @cspiel1 in https://github.com/baresip/baresip/pull/2122
* webrtc/demo: make https optional by @sreimers in https://github.com/baresip/baresip/pull/2120
* Restored original working behavior in uag request\_handler by @juha-h in https://github.com/baresip/baresip/pull/2124
* uag: out-of-dialog REFER handler checks to.tag by @cspiel1 in https://github.com/baresip/baresip/pull/2125
* Update media fixes by @cspiel1 in https://github.com/baresip/baresip/pull/2116
* account: set 100rel default to no by @cspiel1 in https://github.com/baresip/baresip/pull/2128
* avcodec: remove usage of old FFmpeg api (before 4.1.9) by @alfredh in https://github.com/baresip/baresip/pull/2126
* rtp: Improve media synchronization by @sreimers in https://github.com/baresip/baresip/pull/2129
* avformat: remove usage of old FFmpeg api by @alfredh in https://github.com/baresip/baresip/pull/2130
* i2s: remove deprecated module by @alfredh in https://github.com/baresip/baresip/pull/2131
* ci: migrate to CMake by @alfredh in https://github.com/baresip/baresip/pull/2132
* menu: during early media switch on/off ringback by @cspiel1 in https://github.com/baresip/baresip/pull/2133
* call, event, audio: send DTMF via hidden call by @cspiel1 in https://github.com/baresip/baresip/pull/2134
* ua,reg,serreg: fix serial registration mode by @cspiel1 in https://github.com/baresip/baresip/pull/2139
* CodeQL fixes by @sreimers in https://github.com/baresip/baresip/pull/2143
* cmake: set atomic-implicit-seq-cst only for C language by @sreimers in https://github.com/baresip/baresip/pull/2145
* cmake: define -Wshorten-64-to-32 C only by @sreimers in https://github.com/baresip/baresip/pull/2146
* Cmake of webrtc\_aec module plus remove of unused aec.cpp var by @juha-h in https://github.com/baresip/baresip/pull/2144
* cmake: make include dir public by @sreimers in https://github.com/baresip/baresip/pull/2147
* cmake: add APP\_MODULES and APP\_MODULES\_DIR by @sreimers in https://github.com/baresip/baresip/pull/2148
* Added cmake of gst module by @juha-h in https://github.com/baresip/baresip/pull/2149
* Improved call closed message by @juha-h in https://github.com/baresip/baresip/pull/2151
* gtk & menu: Fix potential memory leaks by @maximilianfridrich in https://github.com/baresip/baresip/pull/2153
* call: allocate streams after peer\_uri was set by @cspiel1 in https://github.com/baresip/baresip/pull/2154
* dshow/cmake: fix stdc++ linking with MSVC by @sreimers in https://github.com/baresip/baresip/pull/2156
* cmake: fix MSVC library output name by @sreimers in https://github.com/baresip/baresip/pull/2157
* webrtc: add HAVE\_GETOPT by @sreimers in https://github.com/baresip/baresip/pull/2158
* config: ignore dirent.h on win32 by @sreimers in https://github.com/baresip/baresip/pull/2159
* ua: do not duplicate request URI parameters by @maximilianfridrich in https://github.com/baresip/baresip/pull/2152
* cmake: add netroam module by @robert-scheck in https://github.com/baresip/baresip/pull/2170
* cmake: add portaudio module by @robert-scheck in https://github.com/baresip/baresip/pull/2173
* cmake: add jack module by @robert-scheck in https://github.com/baresip/baresip/pull/2172
* avcodec,config: add setting for keyframe interval by @cspiel1 in https://github.com/baresip/baresip/pull/2171
* cmake: add sdl module by @robert-scheck in https://github.com/baresip/baresip/pull/2174
* call: set peer URI early for incoming calls by @cspiel1 in https://github.com/baresip/baresip/pull/2168
* cmake: Add options -DDEFAULT\_CAFILE="…" and -DDEFAULT\_AUDIO\_DEVICE="…" by @robert-scheck in https://github.com/baresip/baresip/pull/2179
* cmake: add gtk module by @robert-scheck in https://github.com/baresip/baresip/pull/2176
* cmake: add opus\_multistream module by @robert-scheck in https://github.com/baresip/baresip/pull/2175
* cmake: synchronize behaviour of -DSHARE\_PATH="…" with GNU Makefiles by @robert-scheck in https://github.com/baresip/baresip/pull/2180
* cmake: synchronize behaviour of -DMOD\_PATH="…" with GNU Makefiles by @robert-scheck in https://github.com/baresip/baresip/pull/2181
* Move docs/COPYING to LICENSE and update content to match with re/rem by @robert-scheck in https://github.com/baresip/baresip/pull/2188
* cmake: add ABI (soname) versioning by @robert-scheck in https://github.com/baresip/baresip/pull/2187
* misc: Use example domains and IPs by @robert-scheck in https://github.com/baresip/baresip/pull/2186
* cmake: symlink modules by @sreimers in https://github.com/baresip/baresip/pull/2190
* cmake: add mpa module by @robert-scheck in https://github.com/baresip/baresip/pull/2191


## [2.7.0] - 2022-09-01

* menu: fix menu_ua_carg data preference by @sreimers in https://github.com/baresip/baresip/pull/2045
* call: remember media dir for established state by @cspiel1 in https://github.com/baresip/baresip/pull/2046
* avformat: fix ffmpeg channel_layout deprecation by @sreimers in https://github.com/baresip/baresip/pull/2048
* cmake: add multicast module by @cHuberCoffee in https://github.com/baresip/baresip/pull/2049
* play: ring tone fixes if file_ausrc is set by @cspiel1 in https://github.com/baresip/baresip/pull/2050
* add peerconnection and mediatrack by @alfredh in https://github.com/baresip/baresip/pull/2054
* main,test: close re async before tmr_debug by @sreimers in https://github.com/baresip/baresip/pull/2055
* http: new file for HTTP functions by @alfredh in https://github.com/baresip/baresip/pull/2056
* http: add http_reply_json() by @alfredh in https://github.com/baresip/baresip/pull/2057
* play: tmr_polling has to check if ausrc is used by @cspiel1 in https://github.com/baresip/baresip/pull/2061
* cmake: use object instead of static for modules by @sreimers in https://github.com/baresip/baresip/pull/2064
* [WIP] import baresip-webrtc by @alfredh in https://github.com/baresip/baresip/pull/2059
* FindAMR.cmake fixes/improvements by @juha-h in https://github.com/baresip/baresip/pull/2066
* cmake: fix modules install path and install share files by @sreimers in https://github.com/baresip/baresip/pull/2068
* hook up webrtc to main cmake file by @alfredh in https://github.com/baresip/baresip/pull/2067
* avformat: check shared for both audio+video by @alfredh in https://github.com/baresip/baresip/pull/2069
* cmake: add V4L2 module by @alfredh in https://github.com/baresip/baresip/pull/2071
* Omx remove by @alfredh in https://github.com/baresip/baresip/pull/2070
* cmake: add directfb module by @alfredh in https://github.com/baresip/baresip/pull/2072
* main,webrtc/main: add re_thread_async_init by @sreimers in https://github.com/baresip/baresip/pull/2076
* cmake: add wincons and winwave modules by @alfredh in https://github.com/baresip/baresip/pull/2077
* cmake: add sndfile module by @alfredh in https://github.com/baresip/baresip/pull/2078
* Mention actual GTK+ 3 usage (instead of 2) in README.md by @robert-scheck in https://github.com/baresip/baresip/pull/2079
* ctrl_tcp: change unsafe operations on an mbuf to the safe mbuf interface by @cHuberCoffee in https://github.com/baresip/baresip/pull/2082
* gzrtp: Call event hander when SAS needs to be verified by @juha-h in https://github.com/baresip/baresip/pull/2081
* Generate also MENC_EVENT_PEER_VERIFIED event by @juha-h in https://github.com/baresip/baresip/pull/2084
* gzrtp: Generate only one MENC_EVENT_PEER_VERIFIED event when all streams are verified by @juha-h in https://github.com/baresip/baresip/pull/2086
* config,net: add use_getaddrinfo/dns_getaddrinfo option by @sreimers in https://github.com/baresip/baresip/pull/2087
* cmake: add_compile_options and use re config by @sreimers in https://github.com/baresip/baresip/pull/2089
* cmake/modules: build syslog only if available by @sreimers in https://github.com/baresip/baresip/pull/2090
* cmake: add selftest by @sreimers in https://github.com/baresip/baresip/pull/2093
* cmake: add win32 linklibs by @sreimers in https://github.com/baresip/baresip/pull/2091
* cmake: add mqtt by @sreimers in https://github.com/baresip/baresip/pull/2094
* Improve C11 cchecks by @sreimers in https://github.com/baresip/baresip/pull/2096
* Added cmake of gzrtp module by @juha-h in https://github.com/baresip/baresip/pull/2095
* Gzrtp cmake by @juha-h in https://github.com/baresip/baresip/pull/2102
* Added cmake of webrtc_aecm module by @juha-h in https://github.com/baresip/baresip/pull/2104
* Suppressed unused var warnings in webrtc_aecm module by @juha-h in https://github.com/baresip/baresip/pull/2105
* call: do not set call state to answered, if session progress (PRACK) by @RobertMi21 in https://github.com/baresip/baresip/pull/2106

---

## [2.6.0] - 2022-08-01

* conf: check input arguments by @alfredh in https://github.com/baresip/baresip/pull/1932
* dtls_srtp: print TLS cipher name by @alfredh in https://github.com/baresip/baresip/pull/1933
* cmake: add AAC module by @alfredh in https://github.com/baresip/baresip/pull/1935
* call, menu: make selective early media RFC-3261 conform by @cspiel1 in https://github.com/baresip/baresip/pull/1929
* config: add flag to enable/disable linklocal by @alfredh in https://github.com/baresip/baresip/pull/1934
* audio: update filters if codec changes by @cspiel1 in https://github.com/baresip/baresip/pull/1937
* Fix CMake fails when OpenSSL is not present by @widgetii in https://github.com/baresip/baresip/pull/1939
* sip: add RFC 3262 support by @maximilianfridrich in https://github.com/baresip/baresip/pull/1930
* Add CMake target to install baresip executable, library and modules by @widgetii in https://github.com/baresip/baresip/pull/1940
* audio: fix SEGV if stream_alloc() fails by @cspiel1 in https://github.com/baresip/baresip/pull/1942
* gst_video: remove deprecated module by @alfredh in https://github.com/baresip/baresip/pull/1943
* ci: test cmake by @alfredh in https://github.com/baresip/baresip/pull/1944
* cmake: add aptx module by @alfredh in https://github.com/baresip/baresip/pull/1945
* avcodec: remove avcodec_free_context wrapper by @alfredh in https://github.com/baresip/baresip/pull/1947
* avcodec: remove old call to avcodec_init() by @alfredh in https://github.com/baresip/baresip/pull/1948
* cmake: add ffmpeg modules by @alfredh in https://github.com/baresip/baresip/pull/1949
* cmake: add codec2 module by @alfredh in https://github.com/baresip/baresip/pull/1950
* thread: thrd_error fixes by @sreimers in https://github.com/baresip/baresip/pull/1955
* Revert PR #1922 by @juha-h in https://github.com/baresip/baresip/pull/1964
* sip: add RFC 3311 support by @maximilianfridrich in https://github.com/baresip/baresip/pull/1941
* cmake: add amr module by @alfredh in https://github.com/baresip/baresip/pull/1962
* ci/misc: bump pr-dependency-action@v0.5 by @sreimers in https://github.com/baresip/baresip/pull/1968
* ci: add cmake/macos by @alfredh in https://github.com/baresip/baresip/pull/1961
* Feature: add user data to call by @copiltembel in https://github.com/baresip/baresip/pull/1951
* cmake: add audiounit module by @alfredh in https://github.com/baresip/baresip/pull/1969
* cmake: add avcapture module by @alfredh in https://github.com/baresip/baresip/pull/1970
* cmake: add coreaudio module by @alfredh in https://github.com/baresip/baresip/pull/1972
* audio: remove unused aubuf for decoding by @cspiel1 in https://github.com/baresip/baresip/pull/1974
* Modules cmake by @viordash in https://github.com/baresip/baresip/pull/1975
* Modules cmake by @viordash in https://github.com/baresip/baresip/pull/1978
* audio: always start reading in TX thread by @cspiel1 in https://github.com/baresip/baresip/pull/1979
* audio: always start reading in TX poll mode by @cspiel1 in https://github.com/baresip/baresip/pull/1980
* multicast: always start reading of TX aubuf by @cspiel1 in https://github.com/baresip/baresip/pull/1981
* pulse_async: reduce number of reconnect attempts by @RobertMi21 in https://github.com/baresip/baresip/pull/1977
* ci/build: replace deprecated macos-10.15 by @sreimers in https://github.com/baresip/baresip/pull/1984
* ci/build/macos: disable dbus by @sreimers in https://github.com/baresip/baresip/pull/1985
* Improve RFC 3262 by @maximilianfridrich in https://github.com/baresip/baresip/pull/1973
* call: do not stop streams on session progress by @cspiel1 in https://github.com/baresip/baresip/pull/1986
* audio: revert some TX commits and fix TX poll mode by @cspiel1 in https://github.com/baresip/baresip/pull/1987
* call: fix heap-buffer-overflow in prack_handler by @sreimers in https://github.com/baresip/baresip/pull/1988
* Improve re_atomic handling by @sreimers in https://github.com/baresip/baresip/pull/1982
* mk/ctrl_dbus: fix atomic implicit warnings by @sreimers in https://github.com/baresip/baresip/pull/1991
* cmake: add mixminus module by @sreimers in https://github.com/baresip/baresip/pull/1994
* cmake: add dtls_srtp module by @alfredh in https://github.com/baresip/baresip/pull/1993
* Stun uri cred by @viordash in https://github.com/baresip/baresip/pull/1960
* event: fix wrong place of the err check by @copiltembel in https://github.com/baresip/baresip/pull/1992
* Added mwi module cmake build by @juha-h in https://github.com/baresip/baresip/pull/1995
* call: disable prack_handler temporarily by @sreimers in https://github.com/baresip/baresip/pull/1998
* Fix prack handling by @maximilianfridrich in https://github.com/baresip/baresip/pull/1999
* ci: test re/rem with cmake by @alfredh in https://github.com/baresip/baresip/pull/1997
* Added cmake of zrtp module by @juha-h in https://github.com/baresip/baresip/pull/2005
* Added cmake of zrtp module by @juha-h in https://github.com/baresip/baresip/pull/2006
* Added cmake of uuid module by @juha-h in https://github.com/baresip/baresip/pull/2007
* cmake: fix openssl linking by @sreimers in https://github.com/baresip/baresip/pull/2008
* Load also pulse-simple library if exists by @juha-h in https://github.com/baresip/baresip/pull/2010
* Added cmake of presence module by @juha-h in https://github.com/baresip/baresip/pull/2011
* cmake: add more libs, stable branch and static build by @sreimers in https://github.com/baresip/baresip/pull/2012
* Added cmake of selfview module by @juha-h in https://github.com/baresip/baresip/pull/2014
* Added cmake of vp8 and vp9 modules by @juha-h in https://github.com/baresip/baresip/pull/2016
* Added cmake of g722 module by @juha-h in https://github.com/baresip/baresip/pull/2015
* Added cmake of srtp module by @juha-h in https://github.com/baresip/baresip/pull/2017
* cmake: add module override option by @sreimers in https://github.com/baresip/baresip/pull/2020
* cmake: add EXTRA_MODULES option by @sreimers in https://github.com/baresip/baresip/pull/2021
* ci/cmake: add brew packages by @sreimers in https://github.com/baresip/baresip/pull/2023
* Added cmake of g726 module by @juha-h in https://github.com/baresip/baresip/pull/2022
* cmake: add ctrl_dbus module by @cspiel1 in https://github.com/baresip/baresip/pull/2000
* cmake: refactor module prefix by @sreimers in https://github.com/baresip/baresip/pull/2024
* Added cmake of snapshot module by @juha-h in https://github.com/baresip/baresip/pull/2026
* Cmake add dshow by @alfredh in https://github.com/baresip/baresip/pull/2031
* fakevideo: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2032
* cmake: add evdev module by @alfredh in https://github.com/baresip/baresip/pull/2033
* aubridge: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2035
* ausine: use C11 threads by @alfredh in https://github.com/baresip/baresip/pull/2036
* cmake: check for HAVE_UNISTD_H by @alfredh in https://github.com/baresip/baresip/pull/2039
* cmake,mk: prepare main version for release by @sreimers in https://github.com/baresip/baresip/pull/2040
* gsm: remove deprecated module by @alfredh in https://github.com/baresip/baresip/pull/2034
* cmake: add g7221 module by @alfredh in https://github.com/baresip/baresip/pull/2041

### New Contributors
* @widgetii made their first contribution in https://github.com/baresip/baresip/pull/1939
* @maximilianfridrich made their first contribution in https://github.com/baresip/baresip/pull/1930
* @copiltembel made their first contribution in https://github.com/baresip/baresip/pull/1951

---

## [2.5.0] - 2022-07-01

* audio: add optional decoding buffer by @cspiel1 in https://github.com/baresip/baresip/pull/1842
* audio: RX filter thread needs separate sampv buffer by @cspiel1 in https://github.com/baresip/baresip/pull/1879
* aufile: fix possible data race warning by @cspiel1 in https://github.com/baresip/baresip/pull/1880
* audiounit,coreaudio: fix kAudioObjectPropertyElementMaster deprecation by @sreimers in https://github.com/baresip/baresip/pull/1881
* av1: explicitly check for supported OBU types by @alfredh in https://github.com/baresip/baresip/pull/1882
* audiounit/coreaudio: fix kAudioObjectPropertyElementMain by @sreimers in https://github.com/baresip/baresip/pull/1885
* ci/build: bump macos min. sdk to 10.12 by @sreimers in https://github.com/baresip/baresip/pull/1883
* ci: run only for pull requests and main branch by @sreimers in https://github.com/baresip/baresip/pull/1887
* multicast: C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1892
* dtls_srtp: enable ECC by default, remove RSA by @alfredh in https://github.com/baresip/baresip/pull/1891
* ci/build: add ubuntu 22.04 by @sreimers in https://github.com/baresip/baresip/pull/1890
* test: add check for memory leaks by @sreimers in https://github.com/baresip/baresip/pull/1896
* stream,metric: RX real-time - make metric thread-safe by @cspiel1 in https://github.com/baresip/baresip/pull/1895
* Cmake findre by @alfredh in https://github.com/baresip/baresip/pull/1893
* test: wait for both audio and video to be established by @alfredh in https://github.com/baresip/baresip/pull/1903
* docs: remove old TODO file by @alfredh in https://github.com/baresip/baresip/pull/1902
* audio: fixed check for aubuf started flag by @cspiel1 in https://github.com/baresip/baresip/pull/1904
* use new mutex interface by @cspiel1 in https://github.com/baresip/baresip/pull/1905
* audio: make rx.filtl thread-safe by @cspiel1 in https://github.com/baresip/baresip/pull/1897
* audio: allocate correct buffer size for static auplay srate by @cspiel1 in https://github.com/baresip/baresip/pull/1906
* Pulseaudio Async Interface Module by @cHuberCoffee in https://github.com/baresip/baresip/pull/1907
* Do not destroy register client when it is unregistered by @juha-h in https://github.com/baresip/baresip/pull/1908
* Two spaces are required after email address by @juha-h in https://github.com/baresip/baresip/pull/1909
* cmake: add alsa module by @alfredh in https://github.com/baresip/baresip/pull/1910
* cmake: fix static openssl and thread linking by @sreimers in https://github.com/baresip/baresip/pull/1911
* In start_registering, create register clients if reg list is empty by @juha-h in https://github.com/baresip/baresip/pull/1913
* ctrl_dbus: use new thread and mtx interface by @cspiel1 in https://github.com/baresip/baresip/pull/1916
* cmake: add pulse and pulse_async module by @cHuberCoffee in https://github.com/baresip/baresip/pull/1919
* Un-subscribe mwi at un-register by @juha-h in https://github.com/baresip/baresip/pull/1918
* call: update media on session progress. by @RobertMi21 in https://github.com/baresip/baresip/pull/1922
* ctrl_dbus send event in main thread by @cspiel1 in https://github.com/baresip/baresip/pull/1921
* uag: add timestamps to SIP trace by @cspiel1 in https://github.com/baresip/baresip/pull/1914
* main: fix open timers check by @sreimers in https://github.com/baresip/baresip/pull/1925
* cmake: add account module by @alfredh in https://github.com/baresip/baresip/pull/1926


---

## [2.4.0] - 2022-06-01

* mulitcast unmute bad quality by @cspiel1 in https://github.com/baresip/baresip/pull/1821
* menu ringback for parallel call by @cspiel1 in https://github.com/baresip/baresip/pull/1827
* multicast: support error code EAGAIN of jbuf_get() by @cspiel1 in https://github.com/baresip/baresip/pull/1832
* use RTP clock rate for timestamp calculation by @cspiel1 in https://github.com/baresip/baresip/pull/1834
* av1 obu by @alfredh in https://github.com/baresip/baresip/pull/1835
* av1 packetizer by @alfredh in https://github.com/baresip/baresip/pull/1836
* av1: depacketizer by @alfredh in https://github.com/baresip/baresip/pull/1837
* Disabled debug statement by @juha-h in https://github.com/baresip/baresip/pull/1838
* h264: move from rem to re by @sreimers in https://github.com/baresip/baresip/pull/1839
* ua: send new event UA_EVENT_CREATE at successful ua allocation by @cHuberCoffee in https://github.com/baresip/baresip/pull/1840
* evdev: fix wrong ioctl size by @sreimers in https://github.com/baresip/baresip/pull/1843
* aufile: ausrc_prm has to be copied when source is allocated by @cspiel1 in https://github.com/baresip/baresip/pull/1844
* conf: missing pointer initialization found by clang analyzer by @cspiel1 in https://github.com/baresip/baresip/pull/1845
* mk/modules: fix omx RPI detection by @sreimers in https://github.com/baresip/baresip/pull/1847
* auconv: add auconv_to_float (fixes #1833) by @alfredh in https://github.com/baresip/baresip/pull/1849
* avfilter: migrate to C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1850
* avformat: C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1851
* selfview: C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1852
* audio: C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1853
* metric: C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1854
* play: C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1855
* dns: add query cache by @sreimers in https://github.com/baresip/baresip/pull/1848
* video: C11 mutex by @alfredh in https://github.com/baresip/baresip/pull/1856
* aufile: C11 threads by @alfredh in https://github.com/baresip/baresip/pull/1858
* audio: add more locking by @alfredh in https://github.com/baresip/baresip/pull/1857
* aufile/play: fix run data race by @sreimers in https://github.com/baresip/baresip/pull/1859
* mc: multicast receiver enable state fix by @cHuberCoffee in https://github.com/baresip/baresip/pull/1861
* audio: C11 thread by @alfredh in https://github.com/baresip/baresip/pull/1860
* av1: add packetize handler by @alfredh in https://github.com/baresip/baresip/pull/1865
* net/net_debug: add default route hint by @sreimers in https://github.com/baresip/baresip/pull/1864
* ice: fix local prio calculation by @sreimers in https://github.com/baresip/baresip/pull/1863
* avformat: open codec if not passthrough by @alfredh in https://github.com/baresip/baresip/pull/1866
* dtls_srtp: Minor whitespace fix by @robert-scheck in https://github.com/baresip/baresip/pull/1870
* vp8: add packetize handler by @alfredh in https://github.com/baresip/baresip/pull/1868
* vp9: add packetizer by @alfredh in https://github.com/baresip/baresip/pull/1871
* debug_cmd: support absolute path for command aufileinfo by @cspiel1 in https://github.com/baresip/baresip/pull/1875
* event: add diverter URI to UA event by @cspiel1 in https://github.com/baresip/baresip/pull/1876
* aufileinfo with synchronous response by @cspiel1 in https://github.com/baresip/baresip/pull/1877

---

## [2.3.0] - 2022-05-01

* mc: multicast mute function by @cHuberCoffee in https://github.com/baresip/baresip/pull/1805
* mc: reject incoming call if high prio multicast is received by @cHuberCoffee in https://github.com/baresip/baresip/pull/1804
* mc: mcplayer stream fade-out and fade-in by @cHuberCoffee in https://github.com/baresip/baresip/pull/1802
* clean_number now will remove all non-digit chars by @mbattista in https://github.com/baresip/baresip/pull/1806
* Workflows cmakelint by @alfredh in https://github.com/baresip/baresip/pull/1808
* ccheck: check all CMakeLists.txt files by @sreimers in https://github.com/baresip/baresip/pull/1810
* mk: remove win32 MSVC project files by @alfredh in https://github.com/baresip/baresip/pull/1811
* cmake: add modules by @sreimers in https://github.com/baresip/baresip/pull/1812
* ajb,aubuf: timestamp is given in [us] by @cspiel1 in https://github.com/baresip/baresip/pull/1809
* call: allow optional leading space in SIP INFO for dtmf-relay by @thomas-karl in https://github.com/baresip/baresip/pull/1814
* conf: add fs_file_extension() by @alfredh in https://github.com/baresip/baresip/pull/1816
* Updated debian version by @juha-h in https://github.com/baresip/baresip/pull/1817
* pulse: fix timestamp integer overrun for arm by @cspiel1 in https://github.com/baresip/baresip/pull/1818
* fix audio multicast artefacts by @cspiel1 in https://github.com/baresip/baresip/pull/1819
* audio: flush aubuf if ssrc changes by @cspiel1 in https://github.com/baresip/baresip/pull/1822
* Debian control dependency update by @juha-h in https://github.com/baresip/baresip/pull/1823
* pulse: support restart of pulseaudio during stream by @cspiel1 in https://github.com/baresip/baresip/pull/1824
* version 2.3.0 by @alfredh in https://github.com/baresip/baresip/pull/1826

### New Contributors
* @thomas-karl made their first contribution in https://github.com/baresip/baresip/pull/1814

---

## [2.0.2] - 2022-04-09

* Added API function call_diverteruri by @juha-h in https://github.com/baresip/baresip/pull/1780
* Avoid undeclared 'CLOCK_REALTIME' on RHEL/CentOS 7 (fixes #1781) by @robert-scheck in https://github.com/baresip/baresip/pull/1782
* audio: add lock in audio_send_digit by @GGGO in https://github.com/baresip/baresip/pull/1786
* vumeter: use new auframe_level() by @sreimers in https://github.com/baresip/baresip/pull/1788
* reg.c: use already declared acc by @GGGO in https://github.com/baresip/baresip/pull/1789
* aubuf adaptive jitter buffer by @cspiel1 in https://github.com/baresip/baresip/pull/1784
* multicast set aubuf silence by @cspiel1 in https://github.com/baresip/baresip/pull/1791
* ccheck: fix line number in error print by @cspiel1 in https://github.com/baresip/baresip/pull/1793
* test: check the correct stream in UA_EVENT_CALL_MENC by @alfredh in https://github.com/baresip/baresip/pull/1794
* audio: missing lock around stream_send by @GGGO in https://github.com/baresip/baresip/pull/1796
* docs: remove obsolete jitter_buffer_wish from config example by @cspiel1 in https://github.com/baresip/baresip/pull/1798
* Multicast jbuf and aubuf changes by @cHuberCoffee in https://github.com/baresip/baresip/pull/1797
* uag: uag_hold_resume() should not return err if there is no call to hold by @cspiel1 in https://github.com/baresip/baresip/pull/1799
* stream: remove mbuf_get_left check in rtp_handler by @GGGO in https://github.com/baresip/baresip/pull/1801
* cmake: preliminary support by @alfredh in https://github.com/baresip/baresip/pull/1800

### New Contributors
* @GGGO made their first contribution in https://github.com/baresip/baresip/pull/1786

---

## [2.0.1] - 2022-03-27

### What's Changed
* audio: fix rx_thread (adaptive jitter buffer) by @sreimers in https://github.com/baresip/baresip/pull/1769
* test: init fixture by @alfredh in https://github.com/baresip/baresip/pull/1772
* test: refactoring of test_account_uri_complete by @alfredh in https://github.com/baresip/baresip/pull/1773
* mk: check also if extensions/XShm.h is present by @cspiel1 in https://github.com/baresip/baresip/pull/1774
* menu: support custom SIP headers by @cspiel1 in https://github.com/baresip/baresip/pull/1775
* menu: use new sdp_dir_decode by @cspiel1 in https://github.com/baresip/baresip/pull/1776
* menu: avoid multiple hash entries with same key by @cspiel1 in https://github.com/baresip/baresip/pull/1777
* menu: support audio file config value "none" by @cspiel1 in https://github.com/baresip/baresip/pull/1778
* intercom: add video preview call by @cspiel1 in https://github.com/baresip/baresip/pull/1779

---

## [2.0.0] - 2022-03-11

### What's Changed
* debug_cmd: use module_event() for aufileinfo events by @cspiel1 in https://github.com/baresip/baresip/pull/1345
* multicast: use module_event() for sending events by @cspiel1 in https://github.com/baresip/baresip/pull/1346
* ctrl_dbus: use module_event() to send exported event by @cspiel1 in https://github.com/baresip/baresip/pull/1347
* ua,call: add CALL_EVENT_OUTGOING by @cspiel1 in https://github.com/baresip/baresip/pull/1348
* GTK caller history by @mbattista in https://github.com/baresip/baresip/pull/1350
* Convert FRITZ!Box XML phone book into Baresip contacts by @robert-scheck in https://github.com/baresip/baresip/pull/1382
* menu: play ringtone on audio_alert device by @cspiel1 in https://github.com/baresip/baresip/pull/1396
* menu: use str_isset() for command parameter by @cspiel1 in https://github.com/baresip/baresip/pull/1397
* dtls_srtp: use elliptic curve cryptography by @cHuberCoffee in https://github.com/baresip/baresip/pull/1385
* Support for s16 playback in jack. Needed for play tones by @srperens in https://github.com/baresip/baresip/pull/1399
* Check that account ;sipnat param has valid value by @juha-h in https://github.com/baresip/baresip/pull/1401
* Tls sipcert per acc by @cHuberCoffee in https://github.com/baresip/baresip/pull/1376
* Vidsrc add packet handler by @alfredh in https://github.com/baresip/baresip/pull/1402
* ToS for video and sip by @cspiel1 in https://github.com/baresip/baresip/pull/1393
* account: add accounts parameter to force media address family by @cspiel1 in https://github.com/baresip/baresip/pull/1395
* Selective early media by @cspiel1 in https://github.com/baresip/baresip/pull/1398
* ua,uag: split ua.c and uag.c by @cspiel1 in https://github.com/baresip/baresip/pull/1349
* Account media af template by @cspiel1 in https://github.com/baresip/baresip/pull/1406
* account: add missing client certificate parameter to template by @cHuberCoffee in https://github.com/baresip/baresip/pull/1408
* account: update answermode values in template by @cspiel1 in https://github.com/baresip/baresip/pull/1405
* menu: command uafind raises UA to head by @cspiel1 in https://github.com/baresip/baresip/pull/1407
* ctrl_dbus: fix possible memleak on failed initialization by @cspiel1 in https://github.com/baresip/baresip/pull/1410
* video passthrough by @alfredh in https://github.com/baresip/baresip/pull/1418
* menu: enable auto answer calls also for command dialdir by @cspiel1 in https://github.com/baresip/baresip/pull/1412
* menu: add command for settings media local direction by @cspiel1 in https://github.com/baresip/baresip/pull/1413
* Accounts addr params by @cspiel1 in https://github.com/baresip/baresip/pull/1414
* Accounts example cleanup by @cspiel1 in https://github.com/baresip/baresip/pull/1415
* menu,call: fix hangup for outgoing call by @cspiel1 in https://github.com/baresip/baresip/pull/1417
* multicast: add source and player API calls by @cHuberCoffee in https://github.com/baresip/baresip/pull/1403
* menu: add command /uareg by @alfredh in https://github.com/baresip/baresip/pull/1421
* menu: return complete URI for commands dial,dialdir by @cspiel1 in https://github.com/baresip/baresip/pull/1424
* menu: in command dialdir call uag_find_requri() with uri by @cspiel1 in https://github.com/baresip/baresip/pull/1425
* gst: replace variable length array (buf) with mem_zalloc by @sreimers in https://github.com/baresip/baresip/pull/1426
* menu: avoid possible memleaks for dial/dialdir commands by @cspiel1 in https://github.com/baresip/baresip/pull/1430
* uag: use local cuser for selecting user-agent (#1433) by @cspiel1 in https://github.com/baresip/baresip/pull/1434
* Work on Intercom module by @cspiel1 in https://github.com/baresip/baresip/pull/1432
* Attended Transfer on GTK by @mbattista in https://github.com/baresip/baresip/pull/1435
* Update README.md with configuration suggestion by @webstean in https://github.com/baresip/baresip/pull/1438
* README fixes by @juha-h in https://github.com/baresip/baresip/pull/1440
* Accounts examples and template by @cspiel1 in https://github.com/baresip/baresip/pull/1441
* serreg: use a timer for registration restart by @cspiel1 in https://github.com/baresip/baresip/pull/1445
* gst: audio playback not correct for some WAV files. by @RobertMi21 in https://github.com/baresip/baresip/pull/1442
* Working on intercom (ringtone override) by @cspiel1 in https://github.com/baresip/baresip/pull/1436
* Use line number 0 if user did not provide any line number by @negbie in https://github.com/baresip/baresip/pull/1451
* AMR Bandwidth Efficient mode support by @srperens in https://github.com/baresip/baresip/pull/1423
* Working on Intercom (menu: allow other modules to reject a call) by @cspiel1 in https://github.com/baresip/baresip/pull/1437
* auframe: add samplerate and channels by @sreimers in https://github.com/baresip/baresip/pull/1452
* account: comment out very basic example in template by @cspiel1 in https://github.com/baresip/baresip/pull/1458
* call answer media dir by @cspiel1 in https://github.com/baresip/baresip/pull/1449
* Account auto answer beep by @cspiel1 in https://github.com/baresip/baresip/pull/1461
* serreg: unregister correct User-Agents on registration failure by @cspiel1 in https://github.com/baresip/baresip/pull/1462
* mk: enable auto-detect of av1 module by @alfredh in https://github.com/baresip/baresip/pull/1463
* ctrl dbus makefile depends by @cspiel1 in https://github.com/baresip/baresip/pull/1457
* stream: check if media is present before enabling the RTP timeout by @cspiel1 in https://github.com/baresip/baresip/pull/1465
* ctrl_dbus: generate dbus code and documentation in makefile by @cspiel1 in https://github.com/baresip/baresip/pull/1456
* auframe: always set srate and ch by @janh in https://github.com/baresip/baresip/pull/1468
* auto answer beep per alert info URI by @cspiel1 in https://github.com/baresip/baresip/pull/1466
* auframe: move to rem by @sreimers in https://github.com/baresip/baresip/pull/1470
* mixminus: add conference feature by @sreimers in https://github.com/baresip/baresip/pull/1411
* vidbridge: check vidbridge_disp_display args fixes segfault by @sreimers in https://github.com/baresip/baresip/pull/1471
* gst: fixed some memory leaks by @RobertMi21 in https://github.com/baresip/baresip/pull/1476
* ua, menu: move auto answer delay handling to menu (#1474) by @cspiel1 in https://github.com/baresip/baresip/pull/1475
* ua,menu: move handling of ANSWERMODE_AUTO to menu (#1474) by @cspiel1 in https://github.com/baresip/baresip/pull/1478
* ausine: support for multiple samplerates by @alfredh in https://github.com/baresip/baresip/pull/1479
* account: fix IPv6 only URI for account_uri_complete() by @cspiel1 in https://github.com/baresip/baresip/pull/1472
* ilbc: remove deprecated module by @alfredh in https://github.com/baresip/baresip/pull/1483
* aubridge/device: remove unused sampv_out (old resample code) by @sreimers in https://github.com/baresip/baresip/pull/1484
* pkg-config version check by @sreimers in https://github.com/baresip/baresip/pull/1481
* mk: support more locations for libre.pc and librem.pc by @cspiel1 in https://github.com/baresip/baresip/pull/1486
* net: remove unused domain by @alfredh in https://github.com/baresip/baresip/pull/1489
* audio: fix aufilt_setup update handling by @sreimers in https://github.com/baresip/baresip/pull/1498
* SIP redirect callbackfunction by @cHuberCoffee in https://github.com/baresip/baresip/pull/1495
* add secure websocket tls context by @sreimers in https://github.com/baresip/baresip/pull/1499
* test: add stunuri by @alfredh in https://github.com/baresip/baresip/pull/1503
* turn: refactoring, add compv by @alfredh in https://github.com/baresip/baresip/pull/1505
* fmt: add string to bool function by @cspiel1 in https://github.com/baresip/baresip/pull/1501
* mk: check glib-2.0 at least like in ubuntu 18.04 by @cspiel1 in https://github.com/baresip/baresip/pull/1507
* registration fixes by @cspiel1 in https://github.com/baresip/baresip/pull/1510
* uag,menu: add commands to enable/disable UDP/TCP/TLS by @cspiel1 in https://github.com/baresip/baresip/pull/1502
* config,audio: add setting audio.telev_pt by @cspiel1 in https://github.com/baresip/baresip/pull/1509
* stream: fix telephone event (#1494) by @cspiel1 in https://github.com/baresip/baresip/pull/1506
* Fix I2S compile error, use auframe by @andreaswatch in https://github.com/baresip/baresip/pull/1512
* ci/tools: fix pylint by @sreimers in https://github.com/baresip/baresip/pull/1515
* config: not all audio config was printed by @cspiel1 in https://github.com/baresip/baresip/pull/1516
* net: replace network_if_getname with net_if_getname by @sreimers in https://github.com/baresip/baresip/pull/1518
* account: add setting audio payload type for telephone-event by @cspiel1 in https://github.com/baresip/baresip/pull/1517
* uag,menu: simplify transport enable/disable and support also ws/wss by @cspiel1 in https://github.com/baresip/baresip/pull/1514
* rst: remove deprecated module by @alfredh in https://github.com/baresip/baresip/pull/1519
* turn: add TCP and TLS transports by @alfredh in https://github.com/baresip/baresip/pull/1520
* speex_pp: remove deprecated module by @alfredh in https://github.com/baresip/baresip/pull/1521
* call: allow video calls by only rejecting a call without any common codecs by @cHuberCoffee in https://github.com/baresip/baresip/pull/1523
* multicast: add missing join for multicast addresses by @cHuberCoffee in https://github.com/baresip/baresip/pull/1524
* confg,uag: rework on sip_transports setting by @cspiel1 in https://github.com/baresip/baresip/pull/1525
* ua: check if peer is capable of video for early video by @cHuberCoffee in https://github.com/baresip/baresip/pull/1526
* mqtt/subscribe: replace fixed command buf and increase response size by @sreimers in https://github.com/baresip/baresip/pull/1527
* mqtt: add reconnect handling (lost broker connection) by @sreimers in https://github.com/baresip/baresip/pull/1528
* event: increase module_event buffer size by @sreimers in https://github.com/baresip/baresip/pull/1532
* mqtt/subscribe: use safe odict_string to prevent crashes by @sreimers in https://github.com/baresip/baresip/pull/1534
* stream: add stream_set_label by @alfredh in https://github.com/baresip/baresip/pull/1537
* Makefile dependency check improvements by @sreimers in https://github.com/baresip/baresip/pull/1531
* account: add enable/disable flag for video by @cspiel1 in https://github.com/baresip/baresip/pull/1536
* audio: use account specific audio telev pt correctly by @cspiel1 in https://github.com/baresip/baresip/pull/1542
* net: add missing HAVE_INET6 by @cspiel1 in https://github.com/baresip/baresip/pull/1543
* account: remove unused API function for video enable by @cspiel1 in https://github.com/baresip/baresip/pull/1544
* gst: changed log level for end of file message by @RobertMi21 in https://github.com/baresip/baresip/pull/1548
* multicast: add new configurable multicast TTL config parameter by @cHuberCoffee in https://github.com/baresip/baresip/pull/1545
* call: fix early video capability check (wrong SDP direction checked) by @cHuberCoffee in https://github.com/baresip/baresip/pull/1549
* audio: catch end of file message in ausrc error handler (#1539) by @RobertMi21 in https://github.com/baresip/baresip/pull/1550
* menu: added stopringing command by @RobertMi21 in https://github.com/baresip/baresip/pull/1551
* stream: remove obsolete rx.jbuf_started by @cspiel1 in https://github.com/baresip/baresip/pull/1552
* ua: downgrade level of message "ua: using best effort AF" by @viordash in https://github.com/baresip/baresip/pull/1553
* outgoing calls early callid by @cspiel1 in https://github.com/baresip/baresip/pull/1547
* audio: changed log level for ausrc error handler messages by @RobertMi21 in https://github.com/baresip/baresip/pull/1554
* SIP default protocol by @cspiel1 in https://github.com/baresip/baresip/pull/1538
* serreg: fix server selection in case all server were unavailable by @cHuberCoffee in https://github.com/baresip/baresip/pull/1557
* multicast: fix missing unlock by @alfredh in https://github.com/baresip/baresip/pull/1559
* config: replace strcpy by saver re_snprintf (#1558) by @cspiel1 in https://github.com/baresip/baresip/pull/1560
* multicast: fix coverity scan by @alfredh in https://github.com/baresip/baresip/pull/1561
* odict: hide struct odict_entry by @sreimers in https://github.com/baresip/baresip/pull/1562
* ctrl_dbus: use mqueue to trigger processing of command in remain thread by @cspiel1 in https://github.com/baresip/baresip/pull/1565
* multicast,config: add separate jitter buffer configuration by @cspiel1 in https://github.com/baresip/baresip/pull/1566
* ua: emit CALL_CLOSED event when user agent is deleted by @cspiel1 in https://github.com/baresip/baresip/pull/1564
* core: move stream_enable_rtp_timeout to api by @sreimers in https://github.com/baresip/baresip/pull/1569
* stream: add mid sdp attribute by @alfredh in https://github.com/baresip/baresip/pull/1570
* rtpext: change length type to size_t by @alfredh in https://github.com/baresip/baresip/pull/1573
* avcodec: remove old backwards compat wrapper by @alfredh in https://github.com/baresip/baresip/pull/1575
* main: Added option (-a) to set the ua agent string. by @RobertMi21 in https://github.com/baresip/baresip/pull/1576
* menu fix tones for parallel outgoing calls by @cspiel1 in https://github.com/baresip/baresip/pull/1577
* Fix win32 by @viordash in https://github.com/baresip/baresip/pull/1579
* Fix static analyzer warnings by @viordash in https://github.com/baresip/baresip/pull/1580
* call: added auto dtmf mode by @RobertMi21 in https://github.com/baresip/baresip/pull/1583
* RTP inbound telephone events should not lead to packet loss by @cspiel1 in https://github.com/baresip/baresip/pull/1581
* Running tests in a win32 project  by @viordash in https://github.com/baresip/baresip/pull/1585
* stream: wrong media direction after setting stream to hold by @RobertMi21 in https://github.com/baresip/baresip/pull/1587
* move network check to module by @cspiel1 in https://github.com/baresip/baresip/pull/1584
* serreg: do not ignore returned errors of ua_register() by @cspiel1 in https://github.com/baresip/baresip/pull/1589
* Bundle media mux by @alfredh in https://github.com/baresip/baresip/pull/1588
* mixausrc: no warnings flood when sampc changes by @cspiel1 in https://github.com/baresip/baresip/pull/1595
* ua: select laddr with route to SDP offer address by @cspiel1 in https://github.com/baresip/baresip/pull/1590
* net,uag: allow incoming peer-to-peer calls with user@domain by @cspiel1 in https://github.com/baresip/baresip/pull/1591
* uag: in uag_reset_transp() select laddr with route to SDP raddr by @cspiel1 in https://github.com/baresip/baresip/pull/1592
* uag: exit if transport could not be added by @cspiel1 in https://github.com/baresip/baresip/pull/1593
* avcodec: use const AVCodec by @alfredh in https://github.com/baresip/baresip/pull/1602
* module: deprecate module_tmp by @alfredh in https://github.com/baresip/baresip/pull/1600
* test: use ausine as audio source by @alfredh in https://github.com/baresip/baresip/pull/1601
* Selftest fakevideo by @alfredh in https://github.com/baresip/baresip/pull/1604
* When adding local address, check that it has not been added already by @juha-h in https://github.com/baresip/baresip/pull/1606
* start without network by @cspiel1 in https://github.com/baresip/baresip/pull/1607
* config: add netroam module by @sreimers in https://github.com/baresip/baresip/pull/1608
* multicast: allow any port number for sender and receiver by @cHuberCoffee in https://github.com/baresip/baresip/pull/1609
* netroam: add netlink immediate network change detection by @cspiel1 in https://github.com/baresip/baresip/pull/1612
* remove uag transp rm (#1611) by @cspiel1 in https://github.com/baresip/baresip/pull/1616
* net dns srv get by @cspiel1 in https://github.com/baresip/baresip/pull/1615
* move calls to stream_start_rtcp to call.c by @alfredh in https://github.com/baresip/baresip/pull/1617
* video: null pointer check for the display handler by @cspiel1 in https://github.com/baresip/baresip/pull/1621
* audio: add lock by @alfredh in https://github.com/baresip/baresip/pull/1619
* ua: select proper af and laddr for outgoing IP calls by @cspiel1 in https://github.com/baresip/baresip/pull/1618
* audio: lock stream by @alfredh in https://github.com/baresip/baresip/pull/1622
* test: replace mock ausrc with ausine by @alfredh in https://github.com/baresip/baresip/pull/1623
* menu ringback session progress by @cspiel1 in https://github.com/baresip/baresip/pull/1625
* New module providing webrtc aec mobile mode filter by @juha-h in https://github.com/baresip/baresip/pull/1626
* uag: respect setting sip_listen (#1627) by @cspiel1 in https://github.com/baresip/baresip/pull/1628
* select laddr for SDP with respect to net_interface by @cspiel1 in https://github.com/baresip/baresip/pull/1630
* stream: do not start audio during early-video by @cspiel1 in https://github.com/baresip/baresip/pull/1629
* remove struct media_ctx by @alfredh in https://github.com/baresip/baresip/pull/1632
* ci: add libwebrtc-audio-processing-dev (module webrtc_aec) by @sreimers in https://github.com/baresip/baresip/pull/1635
* auconv: new module for audio format conversion by @alfredh in https://github.com/baresip/baresip/pull/1634
* Support for IPv6 link local address for streams by @cspiel1 in https://github.com/baresip/baresip/pull/1624
* call: check if address family is valid also for video stream by @cspiel1 in https://github.com/baresip/baresip/pull/1636
* audio: pass pointer to tx->ausrc_prm instead of local variable by @cspiel1 in https://github.com/baresip/baresip/pull/1637
* menu: add an event for call transfer by @cspiel1 in https://github.com/baresip/baresip/pull/1641
* netroam: error handling for reset transport by @cspiel1 in https://github.com/baresip/baresip/pull/1642
* mk: use CC_TEST for auto detect modules by @sreimers in https://github.com/baresip/baresip/pull/1647
* test: use dtls_srtp.so module instead of mock by @alfredh in https://github.com/baresip/baresip/pull/1646
* stream: create jbuf only if use_rtp is set by @cspiel1 in https://github.com/baresip/baresip/pull/1648
* multicast: fix memleak in player destructor by @cspiel1 in https://github.com/baresip/baresip/pull/1653
* stream: split up sender/receiver by @alfredh in https://github.com/baresip/baresip/pull/1654
* set sdp laddr to SIP src address by @cspiel1 in https://github.com/baresip/baresip/pull/1645
* serreg fix fallback accounts by @cspiel1 in https://github.com/baresip/baresip/pull/1660
* ctrl_dbus: print command with the warning by @cspiel1 in https://github.com/baresip/baresip/pull/1662
* call: new transfer call state to handle transfered calls correctly by @cHuberCoffee in https://github.com/baresip/baresip/pull/1658
* serreg: prevent fast register retries if offline by @cspiel1 in https://github.com/baresip/baresip/pull/1663
* av1: update packetization code by @alfredh in https://github.com/baresip/baresip/pull/1657
* call: magic check in sipsess_desc_handler() by @cspiel1 in https://github.com/baresip/baresip/pull/1664
* alsa: use snd_pcm_drop instead of snd_pcm_drain by @sreimers in https://github.com/baresip/baresip/pull/1669
* Increased debian compat level to 10 by @juha-h in https://github.com/baresip/baresip/pull/1667
* conf: fix conf_configure_buf() config parse by @sreimers in https://github.com/baresip/baresip/pull/1666
* stream flush rtp socket by @cspiel1 in https://github.com/baresip/baresip/pull/1671
* Transfer like rfc5589 by @cHuberCoffee in https://github.com/baresip/baresip/pull/1678
* GTK: mem_derefer call earlier by @mbattista in https://github.com/baresip/baresip/pull/1682
* netroam: add fail counter and event by @cspiel1 in https://github.com/baresip/baresip/pull/1685
* Added API functions stream_metric_get_(tx|rx)_bitrate by @juha-h in https://github.com/baresip/baresip/pull/1686
* Multicast new functions by @cHuberCoffee in https://github.com/baresip/baresip/pull/1687
* avcodec: Enable pass-through for more codecs by @abrodkin in https://github.com/baresip/baresip/pull/1692
* menu: filter for the correct call state in menu_selcall by @cHuberCoffee in https://github.com/baresip/baresip/pull/1693
* test: fix warning on mingw32 by @alfredh in https://github.com/baresip/baresip/pull/1696
* menu: Play ringback in play device by @myrkr in https://github.com/baresip/baresip/pull/1698
* sip: add optional TCP source port by @cspiel1 in https://github.com/baresip/baresip/pull/1695
* rtpext: change id unsigned -> uint8_t by @alfredh in https://github.com/baresip/baresip/pull/1701
* ci: add mingw build test by @sreimers in https://github.com/baresip/baresip/pull/1700
* test: use mediaenc srtp instead of mock by @alfredh in https://github.com/baresip/baresip/pull/1702
* test: remove mock mediaenc by @alfredh in https://github.com/baresip/baresip/pull/1704
* descr: add session_description by @alfredh in https://github.com/baresip/baresip/pull/1706
* use fs_isfile() by @alfredh in https://github.com/baresip/baresip/pull/1709
* stream: only call rtp_clear for audio by @alfredh in https://github.com/baresip/baresip/pull/1710
* checks if call is available before calling call, closes #1708 by @mbattista in https://github.com/baresip/baresip/pull/1712
* conf: add conf_loadfile by @alfredh in https://github.com/baresip/baresip/pull/1713
* ice: remove ice_mode by @sreimers in https://github.com/baresip/baresip/pull/1714
* audio: use auframe in encode_rtp_send, ref #1699 by @alfredh in https://github.com/baresip/baresip/pull/1715
* Increased account's max video codec count from four to eight by @juha-h in https://github.com/baresip/baresip/pull/1717
* gtk: Avoid duplicate call_timer registration by @myrkr in https://github.com/baresip/baresip/pull/1719
* Attended call transfer by @cHuberCoffee in https://github.com/baresip/baresip/pull/1718
* menu: exclude given call when searching for active call by @cspiel1 in https://github.com/baresip/baresip/pull/1721
* menu: play call waiting tone on audio_player device by @cspiel1 in https://github.com/baresip/baresip/pull/1722
* ci/build/macos: link ffmpeg@4 by @sreimers in https://github.com/baresip/baresip/pull/1725
* module auresamp by @cspiel1 in https://github.com/baresip/baresip/pull/1705
* test: remove h264 testcode, already in retest by @alfredh in https://github.com/baresip/baresip/pull/1726
* h265: move from avcodec to rem by @alfredh in https://github.com/baresip/baresip/pull/1728
* mc: send more details at receiver - timeout event by @cHuberCoffee in https://github.com/baresip/baresip/pull/1731
* h265: move packetizer from avcodec to rem by @alfredh in https://github.com/baresip/baresip/pull/1732
* FFmpeg 5 by @sreimers in https://github.com/baresip/baresip/pull/1734
* Fixing clang ThreadSanitizer warnings by @sreimers in https://github.com/baresip/baresip/pull/1730
* auresamp: replace anonymous union for pre C11 compilers by @cspiel1 in https://github.com/baresip/baresip/pull/1738
* aufile: align naming of alloc handlers by @sreimers in https://github.com/baresip/baresip/pull/1739
* auresamp fixes by @cspiel1 in https://github.com/baresip/baresip/pull/1741
* mc: new priority handling with multicast state by @cHuberCoffee in https://github.com/baresip/baresip/pull/1740
* remove support for Solaris platform by @alfredh in https://github.com/baresip/baresip/pull/1745
* Allow hanging up call that has not been ACKed yet by @juha-h in https://github.com/baresip/baresip/pull/1747
* Multicast identical condition and fmt string fix by @cHuberCoffee in https://github.com/baresip/baresip/pull/1751
* audio: allocate aubuf before ausrc_alloc (fixes data race) by @sreimers in https://github.com/baresip/baresip/pull/1748
* call: send supported header for 200 answering/ok by @cHuberCoffee in https://github.com/baresip/baresip/pull/1752
* event: check if media line is present for encoding audio/video dir by @cspiel1 in https://github.com/baresip/baresip/pull/1754
* Removed unused variable in modules/webrtc_aec/aec.cpp by @juha-h in https://github.com/baresip/baresip/pull/1756
* audio use module auconv by @cspiel1 in https://github.com/baresip/baresip/pull/1742
* test: use aufile module by @alfredh in https://github.com/baresip/baresip/pull/1757
* x11grab: remove module, use avformat.so instead by @alfredh in https://github.com/baresip/baresip/pull/1758
* audio: declare iterator inside for-loop (C99) by @alfredh in https://github.com/baresip/baresip/pull/1759
* aufile: set run=true before write thread starts (#1727) by @cspiel1 in https://github.com/baresip/baresip/pull/1762
* Added new API function call_supported() and used it in menu module by @juha-h in https://github.com/baresip/baresip/pull/1761
* aufile: separate aufile_src.c from aufile.c by @cspiel1 in https://github.com/baresip/baresip/pull/1765
* ctrl_dbus: fix possible data race (#1727) by @cspiel1 in https://github.com/baresip/baresip/pull/1764
* menu select other call on hangup by @cspiel1 in https://github.com/baresip/baresip/pull/1763
* event: encode also combined media direction by @cspiel1 in https://github.com/baresip/baresip/pull/1766

## New Contributors
* @srperens made their first contribution in https://github.com/baresip/baresip/pull/1399
* @negbie made their first contribution in https://github.com/baresip/baresip/pull/1451
* @andreaswatch made their first contribution in https://github.com/baresip/baresip/pull/1512
* @viordash made their first contribution in https://github.com/baresip/baresip/pull/1553
* @abrodkin made their first contribution in https://github.com/baresip/baresip/pull/1692
* @myrkr made their first contribution in https://github.com/baresip/baresip/pull/1698

---

## [1.1.0] - 2021-04-24

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

---

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


[Unreleased]: https://github.com/baresip/baresip/compare/v2.7.0...HEAD
[2.7.0]: https://github.com/baresip/baresip/compare/v2.6.0...v2.7.0
[2.6.0]: https://github.com/baresip/baresip/compare/v2.5.0...v2.6.0
[2.5.0]: https://github.com/baresip/baresip/compare/v2.4.0...v2.5.0
[2.4.0]: https://github.com/baresip/baresip/compare/v2.3.0...v2.4.0
[2.3.0]: https://github.com/baresip/baresip/compare/v2.0.2...v2.3.0
[2.0.2]: https://github.com/baresip/baresip/compare/v2.0.1...v2.0.2
[2.0.1]: https://github.com/baresip/baresip/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/baresip/baresip/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/baresip/baresip/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/baresip/baresip/compare/v0.6.6...v1.0.0
