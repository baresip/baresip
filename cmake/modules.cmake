find_package(AAC)
find_package(ALSA)
find_package(AMR)
find_package(APTX)
find_package(AOM)
find_package(CODEC2)
find_package(FFMPEG COMPONENTS
    avutil avcodec avfilter avformat swscale swresample avdevice)
find_package(GLIB)
find_package(OPUS)
find_package(PULSE)
find_package(SPANDSP)
find_package(VPX)
find_package(X11)
find_package(ZRTP)

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
  debug_cmd
  echo
  fakevideo
  g711
  ice
  menu
  mixminus
  mwi
  presence
  selfview
  srtp
  stdio
  stun
  turn
  uuid
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
if(FFMPEG_FOUND)
  list(APPEND MODULES avcodec)
  list(APPEND MODULES avfilter)
  list(APPEND MODULES avformat)
  list(APPEND MODULES swscale)
endif()
if(GLIB_FOUND)
  add_subdirectory(modules/ctrl_dbus)
endif()
if(USE_OPENSSL)
  list(APPEND MODULES dtls_srtp)
endif()
if(OPUS_FOUND)
  list(APPEND MODULES opus)
endif()
if(PULSE_FOUND)
  list(APPEND MODULES pulse)
  list(APPEND MODULES pulse_async)
endif()
if(SPANDSP_FOUND)
  list(APPEND MODULES g722)
endif()
if(VPX_FOUND)
  list(APPEND MODULES vp8)
  list(APPEND MODULES vp9)
endif()
if(X11_FOUND)
  list(APPEND MODULES x11)
endif()
if(ZRTP_FOUND)
  list(APPEND MODULES zrtp)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  list(APPEND MODULES audiounit)
  list(APPEND MODULES avcapture)
  list(APPEND MODULES coreaudio)
endif()

if(DEFINED EXTRA_MODULES)
  list(APPEND MODULES ${EXTRA_MODULES})
endif()
