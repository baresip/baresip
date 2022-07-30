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

include(CheckIncludeFile)
find_package(RE REQUIRED)
find_package(REM REQUIRED)
find_package(OpenSSL)
find_package(Threads REQUIRED)

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

include(GNUInstallDirs)
set(SHARE_PATH ${CMAKE_INSTALL_FULL_DATAROOTDIR}/baresip)

add_definitions(
  -DHAVE_INET6
  -DHAVE_SIGNAL
  -DSHARE_PATH="${SHARE_PATH}"
  )

check_include_file(unistd.h HAVE_UNISTD_H)
if(HAVE_UNISTD_H)
  add_definitions(-DHAVE_UNISTD_H)
endif()

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
  include_directories( ${OPENSSL_INCLUDE_DIR} )
endif()

if(STATIC)
  add_definitions(-DSTATIC)
endif()

if(MSVC)
  add_definitions(
    -DHAVE_IO_H
    -D_CRT_SECURE_NO_WARNINGS
  )
endif()

if(WIN32)
  add_definitions(-DWIN32 -D_WIN32_WINNT=0x0600)
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

set(HEADERS
  include/baresip.h
)

##############################################################################
#
# Modules
#

include(modules)

foreach(mod IN LISTS MODULES)
  add_subdirectory(modules/${mod})
  set_target_properties(${mod} PROPERTIES PREFIX "")
endforeach()

if(STATIC)
  foreach(mod IN LISTS MODULES)
    set(MOD_EXPORTS
      "${MOD_EXPORTS}extern const struct mod_export exports_${mod};\n")
    set(MOD_EXPORT_TABLE
      "${MOD_EXPORT_TABLE}  &exports_${mod},\n")
  endforeach()

  configure_file(src/static.c.in src/static.c)
  list(APPEND SRCS ${CMAKE_CURRENT_BINARY_DIR}/src/static.c)
else()
  foreach(mod IN LISTS MODULES)

    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      set_target_properties(${mod} PROPERTIES
                            LINK_FLAGS "-undefined dynamic_lookup")
    endif()

    install(TARGETS ${mod} LIBRARY DESTINATION ${SHARE_PATH})
  endforeach()
endif()


##############################################################################
#
# Main target library
#

set(LINKLIBS ${REM_LIBRARIES} ${RE_LIBRARIES} Threads::Threads -lz -lm)
if(USE_OPENSSL)
  list(APPEND LINKLIBS OpenSSL::SSL OpenSSL::Crypto)
endif()

if(STATIC)
  add_library(baresip STATIC ${SRCS} ${HEADERS})
  target_link_libraries(baresip PUBLIC ${LINKLIBS} ${MODULES})
else()
  add_library(baresip SHARED ${SRCS} ${HEADERS})
  target_link_libraries(baresip PRIVATE ${LINKLIBS})
endif()

set_target_properties(baresip PROPERTIES POSITION_INDEPENDENT_CODE ON)

set_target_properties(baresip PROPERTIES PUBLIC_HEADER "${HEADERS}")
install(TARGETS baresip LIBRARY)

##############################################################################
#
# Target baresip executable
#

add_executable(baresip_exe src/main.c)

set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip)

target_link_libraries(baresip_exe baresip ${LINKLIBS})
install(TARGETS baresip_exe)
