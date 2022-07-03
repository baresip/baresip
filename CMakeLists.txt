#
# CMakeLists.txt
#
# Copyright (C) 2010 - 2022 Alfred E. Heggestad
# Copyright (C) 2022 Sebastian Reimers
#

##############################################################################
#
# Versioning
#

cmake_minimum_required(VERSION 3.7)

project(baresip VERSION 2.5.0)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

##############################################################################
#
# Module/Package Includes
#

find_package(RE REQUIRED)
find_package(REM REQUIRED)
find_package(OpenSSL)
find_package(Threads REQUIRED)

find_package(AAC)
find_package(ALSA)
find_package(AOM)
find_package(OPUS)
find_package(PULSE)
find_package(X11)


##############################################################################
#
# Compile options
#

option(USE_OPENSSL "Enable OpenSSL" ${OPENSSL_FOUND})
option(STATIC "Build static" OFF)

include_directories(
  include
  src
  ../re/include
  ../rem/include
  /usr/local/include
  ${RE_INCLUDE_DIRS}
  ${REM_INCLUDE_DIRS}
)

link_directories(/usr/local/lib)

add_definitions(
  -DHAVE_GETOPT
  -DHAVE_INET6
  -DHAVE_SIGNAL
  -DHAVE_UNISTD_H
  -DUSE_ZLIB
  -DSHARE_PATH="/usr/share/baresip"
  )

if(USE_OPENSSL)
  add_definitions(
    -DUSE_DTLS
    -DUSE_OPENSSL
    -DUSE_OPENSSL_AES
    -DUSE_OPENSSL_DTLS
    -DUSE_OPENSSL_HMAC
    -DUSE_OPENSSL_SRTP
    -DUSE_TLS
  )
endif()

if(STATIC)
  add_definitions(-DSTATIC)
endif()

##############################################################################
#
# Source section
#

set(SRCS
  src/account.c
  src/aucodec.c
  src/audio.c
  src/aufilt.c
  src/auplay.c
  src/ausrc.c
  src/baresip.c
  src/bundle.c
  src/call.c
  src/cmd.c
  src/conf.c
  src/config.c
  src/contact.c
  src/custom_hdrs.c
  src/descr.c
  src/dial_number.c
  src/event.c
  src/log.c
  src/mediadev.c
  src/menc.c
  src/message.c
  src/metric.c
  src/mnat.c
  src/module.c
  src/net.c
  src/play.c
  src/reg.c
  src/rtpext.c
  src/rtpstat.c
  src/sdp.c
  src/sipreq.c
  src/stream.c
  src/stunuri.c
  src/timestamp.c
  src/ua.c
  src/uag.c
  src/ui.c
  src/vidcodec.c
  src/video.c
  src/vidfilt.c
  src/vidisp.c
  src/vidsrc.c
  src/vidutil.c
)


##############################################################################
#
# Modules
#

set(MODULES "")

add_subdirectory(modules/account)
add_subdirectory(modules/aubridge)
add_subdirectory(modules/auconv)
add_subdirectory(modules/echo)
add_subdirectory(modules/menu)
add_subdirectory(modules/stdio)

if(AAC_FOUND)
  add_subdirectory(modules/aac)
endif()
if(ALSA_FOUND)
  add_subdirectory(modules/alsa)
endif()
if(AOM_FOUND)
  add_subdirectory(modules/av1)
endif()
if(OPUS_FOUND)
  add_subdirectory(modules/opus)
endif()
if(PULSE_FOUND)
  add_subdirectory(modules/pulse)
  add_subdirectory(modules/pulse_async)
endif()
if(X11_FOUND)
  add_subdirectory(modules/x11)
endif()


if(STATIC)
  foreach(mod IN LISTS MODULES)
    set(MOD_EXPORTS
      "${MOD_EXPORTS}extern const struct mod_export exports_${mod};\n")
    set(MOD_EXPORT_TABLE
      "${MOD_EXPORT_TABLE}  &exports_${mod},\n")
  endforeach()

  configure_file(src/static.c.in src/static.c)
  list(APPEND SRCS ${CMAKE_CURRENT_BINARY_DIR}/src/static.c)
endif()


##############################################################################
#
# Main target library
#

if(STATIC)
  add_library(baresip STATIC ${SRCS})
  target_link_libraries(baresip PUBLIC
      ${MODULES} ${REM_LIBRARIES} ${RE_LIBRARIES}
      ${OPENSSL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} -lz -lm)
else()
  add_library(baresip SHARED ${SRCS})
  target_link_libraries(baresip PRIVATE
      ${REM_LIBRARIES} ${RE_LIBRARIES}
      ${OPENSSL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} -lz -lm)
endif()

set_target_properties(baresip PROPERTIES POSITION_INDEPENDENT_CODE ON)


##############################################################################
#
# Target baresip executable
#

add_executable(baresip_exe src/main.c)

set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip)

target_link_libraries(baresip_exe baresip
    ${REM_LIBRARIES} ${RE_LIBRARIES}
    ${OPENSSL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} -lz -lm)
