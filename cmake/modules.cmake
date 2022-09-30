find_package(AAC)
find_package(ALSA)
find_package(AMR)
find_package(APTX)
find_package(AOM)
find_package(CODEC2)
find_package(DIRECTFB)
find_package(FFMPEG COMPONENTS
    avutil avcodec avfilter avformat swscale swresample avdevice)
find_package(G7221)
find_package(GIO)
find_package(GST)
find_package(GTK3)
find_package(GZRTP)
find_package(JACK)
find_package(MOSQUITTO)
find_package(OPENSLES)
find_package(OPUS)
find_package(PNG)
find_package(PORTAUDIO)
find_package(PULSE)
find_package(SDL)
find_package(SNDFILE)
find_package(SPANDSP)
find_package(V4L2)
find_package(VPX)
find_package(WEBRTC_AEC)
find_package(WEBRTC_AECM)
find_package(X11)

if(DEFINED MODULES)
  return()
endif()

set(MODULES
  account
  aubridge
  auconv
  aufile
  auresamp
  ausine
  cons
  contact
  ctrl_tcp
  debug_cmd
  ebuacip
  echo
  fakevideo
  g711
  httpd
  httpreq
  ice
  l16
  menu
  mixausrc
  mixminus
  multicast
  mwi
  natpmp
  netroam
  pcp
  presence
  rtcpsummary
  selfview
  serreg
  srtp
  stun
  turn
  uuid
  vidbridge
  vidinfo
  vumeter
)

if(AAC_FOUND)
  list(APPEND MODULES aac)
endif()
if(ALSA_FOUND)
  list(APPEND MODULES alsa)
endif()
if(AMR_FOUND)
  list(APPEND MODULES amr)
endif()
if(APTX_FOUND)
  list(APPEND MODULES aptx)
endif()
if(AOM_FOUND)
  list(APPEND MODULES av1)
endif()
if(CODEC2_FOUND)
  list(APPEND MODULES codec2)
endif()
if(DIRECTFB_FOUND)
  list(APPEND MODULES directfb)
endif()
if(FFMPEG_FOUND)
  list(APPEND MODULES avcodec)
  list(APPEND MODULES avfilter)
  list(APPEND MODULES avformat)
  list(APPEND MODULES swscale)
endif()
if(GIO_FOUND)
  list(APPEND MODULES ctrl_dbus)
endif()
if(GST_FOUND)
  list(APPEND MODULES gst)
endif()
if(GTK3_FOUND)
  list(APPEND MODULES gtk)
endif()
if(GZRTP_FOUND)
  list(APPEND MODULES gzrtp)
endif()
if(USE_OPENSSL)
  list(APPEND MODULES dtls_srtp)
endif()
if(JACK_FOUND)
  list(APPEND MODULES jack)
endif()
if(MOSQUITTO_FOUND)
  list(APPEND MODULES mqtt)
endif()
if(OPENSLES_FOUND)
  list(APPEND MODULES opensles)
endif()
if(OPUS_FOUND)
  list(APPEND MODULES opus)
  list(APPEND MODULES opus_multistream)
endif()
if(PNG_FOUND)
  list(APPEND MODULES snapshot)
endif()
if(PORTAUDIO_FOUND)
  list(APPEND MODULES portaudio)
endif()
if(PULSE_FOUND)
  list(APPEND MODULES pulse)
  list(APPEND MODULES pulse_async)
endif()
if(SDL_FOUND)
  list(APPEND MODULES sdl)
endif()
if(SNDFILE_FOUND)
  list(APPEND MODULES sndfile)
endif()
if(HAVE_SYSLOG_H)
  list(APPEND MODULES syslog)
endif()
if(SPANDSP_FOUND)
  list(APPEND MODULES g722)
  list(APPEND MODULES g726)
  list(APPEND MODULES plc)
endif()
if(G7221_FOUND)
  list(APPEND MODULES g7221)
endif()
if(V4L2_FOUND)
  list(APPEND MODULES v4l2)
endif()
if(VPX_FOUND)
  list(APPEND MODULES vp8)
  list(APPEND MODULES vp9)
endif()
if(WEBRTC_AEC_FOUND)
  list(APPEND MODULES webrtc_aec)
endif()
if(WEBRTC_AECM_FOUND)
  list(APPEND MODULES webrtc_aecm)
endif()
if(X11_FOUND)
  list(APPEND MODULES x11)
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  list(APPEND MODULES evdev)
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  list(APPEND MODULES audiounit)
  list(APPEND MODULES avcapture)
  list(APPEND MODULES coreaudio)
endif()
if(WIN32)
  list(APPEND MODULES dshow)
  list(APPEND MODULES wincons)
  list(APPEND MODULES winwave)
else()
  list(APPEND MODULES stdio)
endif()

if(DEFINED EXTRA_MODULES)
  list(APPEND MODULES ${EXTRA_MODULES})
endif()
