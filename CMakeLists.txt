#
# CMakeLists.txt
#
# Copyright (C) 2010 - 2022 Alfred E. Heggestad
#

##############################################################################
#
# Versioning
#

cmake_minimum_required(VERSION 3.7)

project(baresip)


##############################################################################
#
# Module/Package Includes
#

find_package(OpenSSL)


##############################################################################
#
# Compile options
#

option(USE_OPENSSL "Enable OpenSSL" ${OPENSSL_FOUND})

include_directories(
  include
  src
  /usr/local/include
  /usr/local/include/re
  /usr/local/include/rem
)

link_directories(/usr/local/lib)

add_definitions(
  -DHAVE_GETOPT
  -DHAVE_INET6
  -DHAVE_PTHREAD
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
  src/main.c
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

  src/main.c
)


add_executable(${PROJECT_NAME}
  ${SRCS}
)
target_link_libraries(${PROJECT_NAME} -lrem -lre -lz -lpthread)
