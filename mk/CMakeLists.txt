cmake_minimum_required(VERSION 3.5)

project(Baresip LANGUAGES C)

option(STATIC "Build baresip and modules together in a single static library"
       OFF)

include(defines.cmake)

set(BARESIP_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules ${CMAKE_MODULE_PATH} CACHE PATH "" FORCE)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL Linux)
  find_package(Opus QUIET)
  find_package(ALSA REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(Jack QUIET)
  find_package(SndFile QUIET)
endif()

set(baresip_source_files
    ${BARESIP_ROOT_DIR}/src/account.c
    ${BARESIP_ROOT_DIR}/src/aucodec.c
    ${BARESIP_ROOT_DIR}/src/audio.c
    ${BARESIP_ROOT_DIR}/src/aufilt.c
    ${BARESIP_ROOT_DIR}/src/auframe.c
    ${BARESIP_ROOT_DIR}/src/aulevel.c
    ${BARESIP_ROOT_DIR}/src/auplay.c
    ${BARESIP_ROOT_DIR}/src/ausrc.c
    ${BARESIP_ROOT_DIR}/src/baresip.c
    ${BARESIP_ROOT_DIR}/src/call.c
    ${BARESIP_ROOT_DIR}/src/cmd.c
    ${BARESIP_ROOT_DIR}/src/conf.c
    ${BARESIP_ROOT_DIR}/src/config.c
    ${BARESIP_ROOT_DIR}/src/contact.c
    ${BARESIP_ROOT_DIR}/src/custom_hdrs.c
    ${BARESIP_ROOT_DIR}/src/event.c
    ${BARESIP_ROOT_DIR}/src/h264.c
    ${BARESIP_ROOT_DIR}/src/log.c
    ${BARESIP_ROOT_DIR}/src/mctrl.c
    ${BARESIP_ROOT_DIR}/src/mediadev.c
    ${BARESIP_ROOT_DIR}/src/menc.c
    ${BARESIP_ROOT_DIR}/src/message.c
    ${BARESIP_ROOT_DIR}/src/metric.c
    ${BARESIP_ROOT_DIR}/src/mnat.c
    ${BARESIP_ROOT_DIR}/src/module.c
    ${BARESIP_ROOT_DIR}/src/net.c
    ${BARESIP_ROOT_DIR}/src/play.c
    ${BARESIP_ROOT_DIR}/src/reg.c
    ${BARESIP_ROOT_DIR}/src/rtpext.c
    ${BARESIP_ROOT_DIR}/src/rtpstat.c
    ${BARESIP_ROOT_DIR}/src/sdp.c
    ${BARESIP_ROOT_DIR}/src/sipreq.c
    ${BARESIP_ROOT_DIR}/src/stream.c
    ${BARESIP_ROOT_DIR}/src/stunuri.c
    ${BARESIP_ROOT_DIR}/src/timer.c
    ${BARESIP_ROOT_DIR}/src/timestamp.c
    ${BARESIP_ROOT_DIR}/src/ua.c
    ${BARESIP_ROOT_DIR}/src/ui.c
    ${BARESIP_ROOT_DIR}/src/vidcodec.c
    ${BARESIP_ROOT_DIR}/src/video.c
    ${BARESIP_ROOT_DIR}/src/vidfilt.c
    ${BARESIP_ROOT_DIR}/src/vidisp.c
    ${BARESIP_ROOT_DIR}/src/vidsrc.c
    ${BARESIP_ROOT_DIR}/src/vidutil.c)

if(STATIC)
  add_library(baresip STATIC ${baresip_source_files})
else()
  add_library(baresip SHARED ${baresip_source_files})
endif()
target_compile_definitions(baresip PRIVATE -DPREFIX=\"${CMAKE_INSTALL_PREFIX}\")
target_compile_definitions(
  baresip PRIVATE -DSHARE_PATH=\"${CMAKE_INSTALL_PREFIX}/share/ua\")
target_link_libraries(baresip PRIVATE re rem m)
target_include_directories(baresip PUBLIC ${BARESIP_ROOT_DIR}/include)

install(
  TARGETS baresip
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib)

install(
  DIRECTORY ${BARESIP_ROOT_DIR}/include/
  DESTINATION include/baresip
  FILES_MATCHING
  PATTERN "*.h")

install(DIRECTORY ${BARESIP_ROOT_DIR}/share/ DESTINATION share/baresip)

set(MODULES "")

macro(module name source_files libs)
  string(REGEX
         REPLACE "[\t ]*([./a-z0-9_]+\.c)"
                 "${BARESIP_ROOT_DIR}/modules/${ARGV0}/\\1;" SRC "${ARGV1}")
  if(STATIC)
    add_library(${ARGV0} STATIC ${SRC})
    target_link_libraries(baresip PRIVATE ${ARGV0})
    target_link_libraries(${ARGV0} PRIVATE baresip rem re ${ARGV2})
    list(APPEND MODULES ${ARGV0})
  else()
    add_library(${ARGV0} SHARED ${SRC})
    set_target_properties(${ARGV0} PROPERTIES PREFIX "")
    target_link_libraries(${ARGV0} PRIVATE baresip rem re ${ARGV2})
    install(TARGETS ${ARGV0} LIBRARY DESTINATION lib/baresip/modules)
  endif()
endmacro(module)

module(account "account.c" "")
if(ALSA_FOUND)
  module(alsa "alsa_play.c alsa_src.c alsa.c" "${ALSA_LIBRARIES}")
endif(ALSA_FOUND)
module(aubridge "aubridge.c device.c src.c play.c" "")
module(auloop "auloop.c" "")
module(aufile "aufile.c" "")
module(cons "cons.c" "")
module(contact "contact.c" "")
module(ctrl_tcp "ctrl_tcp.c tcp_netstring.c ./netstring/netstring.c" "")

module(debug_cmd "debug_cmd.c"
       "${OPENSSL_CRYPTO_LIBRARY};${OPENSSL_SSL_LIBRARY}")
module(dtls_srtp "dtls_srtp.c dtls.c srtp.c" "")
module(g711 "g711.c" "")
module(ice "ice.c" "")
module(menu "menu.c" "")
if(OPUS_FOUND)
  module(opus "decode.c encode.c opus.c sdp.c" "${OPUS_LIBRARIES}")
endif(OPUS_FOUND)

module(rtcpsummary "rtcpsummary.c" "")
if(SNDFILE_FOUND)
  module(sndfile "sndfile.c" "${SNDFILE_LIBRARIES}")
endif()
module(stdio "stdio.c" "")
module(stun "stun.c" "")
module(syslog "syslog.c" "")
module(turn "turn.c" "")
module(uuid "uuid.c" "")
module(vumeter "vumeter.c" "")
module(srtp "srtp.c sdes.c" "")
module(vidloop "vidloop.c" "")

if(CMAKE_SYSTEM_NAME STREQUAL Android)
  module(opensles "opensles.c player.c recorder.c" "OpenSLES")
endif()

# Check for Pulse (any OS)
pkg_check_modules(pulse libpulse-simple)
if(pulse_FOUND)
  find_library(PULSESIMPLE_LIB pulse-simple)
  find_library(PULSE_LIB pulse)
  if(PULSESIMPLE_LIB)
    module(pulse "pulse.c player.c recorder.c" "${PULSESIMPLE_LIB}")
  elseif(PULSE_LIB)
    module(pulse "pulse.c player.c recorder.c" "${PULSE_LIB}")
  endif()
endif(pulse_FOUND)

if(JACK_FOUND)
  include_directories(${JACK_INCLUDE_DIRS})
  module(jack "jack.c jack_play.c jack_src.c" "${JACK_LIBRARIES}")
endif(JACK_FOUND)


if(STATIC)
  add_custom_target(
    generate_mod_init
    COMMAND ${BARESIP_ROOT_DIR}/gen_static_mod_import.sh ${MODULES}
    WORKING_DIRECTORY ${BARESIP_ROOT_DIR}
    COMMENT "Generate the static initialisation for the modules we built"
    BYPRODUCTS ${BARESIP_ROOT_DIR}/src/static.c
    VERBATIM)
  target_sources(baresip PRIVATE ${BARESIP_ROOT_DIR}/src/static.c)
endif()

add_executable(baresip_exe ${BARESIP_ROOT_DIR}/src/main.c)
set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip)
target_link_libraries(baresip_exe PRIVATE baresip rem re)

include(test.cmake)
