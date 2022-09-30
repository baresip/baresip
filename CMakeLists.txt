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

cmake_minimum_required(VERSION 3.10)

project(baresip VERSION 2.7.0)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

##############################################################################
#
# Module/Package Includes
#

include(GNUInstallDirs)
include(CheckIncludeFile)
find_package(RE REQUIRED)
find_package(REM REQUIRED)

##############################################################################
#
# Compile options
#

if(WIN32)
  option(STATIC "Build static" ON)
else()
  option(STATIC "Build static" OFF)
endif()


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
  add_compile_options("/W3")
else()
  add_compile_options(
    -pedantic
    -Wall
    -Wbad-function-cast
    -Wcast-align
    -Wextra
    -Wmissing-declarations
    -Wmissing-prototypes
    -Wnested-externs
    -Wno-strict-aliasing
    -Wold-style-definition
    -Wshadow -Waggregate-return
    -Wstrict-prototypes
    -Wuninitialized
    -Wvla
  )
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:-Watomic-implicit-seq-cst;-Wshorten-64-to-32>"
  )
endif()

find_package(re CONFIG REQUIRED HINTS ../re/cmake)
add_definitions(${RE_DEFINITIONS})

include_directories(
  include
  src
  /usr/local/include
  ${RE_INCLUDE_DIRS}
  ${REM_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
)

link_directories(/usr/local/lib)

if(MOD_PATH)
  add_definitions(-DMOD_PATH="${MOD_PATH}")
elseif(CMAKE_INSTALL_FULL_LIBDIR)
  add_definitions(-DMOD_PATH="${CMAKE_INSTALL_FULL_LIBDIR}/baresip/modules")
endif()

if(SHARE_PATH)
  add_definitions(-DSHARE_PATH="${SHARE_PATH}")
else()
  add_definitions(-DSHARE_PATH="${CMAKE_INSTALL_FULL_DATADIR}/baresip")
endif()

if(DEFAULT_CAFILE)
  add_definitions(-DDEFAULT_CAFILE="${DEFAULT_CAFILE}")
endif()

if(DEFAULT_AUDIO_DEVICE)
  add_definitions(-DDEFAULT_AUDIO_DEVICE="${DEFAULT_AUDIO_DEVICE}")
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
  src/http.c
  src/log.c
  src/mediadev.c
  src/mediatrack.c
  src/menc.c
  src/message.c
  src/metric.c
  src/mnat.c
  src/module.c
  src/net.c
  src/peerconn.c
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

foreach(mod IN LISTS APP_MODULES)
  add_subdirectory(
    ${APP_MODULES_DIR}/${mod}
    ${CMAKE_CURRENT_BINARY_DIR}/app_modules/${mod}
  )
  set_target_properties(${mod} PROPERTIES PREFIX "")
  list(APPEND MODULES ${mod})
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

    install(TARGETS ${mod} LIBRARY DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/baresip/modules)
  endforeach()
endif()


##############################################################################
#
# Main target library
#

set(LINKLIBS ${REM_LIBRARIES} ${RE_LIBRARIES} Threads::Threads)

if(ZLIB_FOUND)
  list(APPEND LINKLIBS ZLIB::ZLIB)
endif()

if(USE_OPENSSL)
  list(APPEND LINKLIBS OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
  list(APPEND LINKLIBS ws2_32 iphlpapi winmm gdi32 crypt32 strmiids
      ole32 oleaut32 qwave)
else()
  list(APPEND LINKLIBS -lm)
endif()

if(STATIC)
  add_library(baresip STATIC ${SRCS} ${HEADERS})
  target_link_libraries(baresip PUBLIC ${LINKLIBS} ${MODULES})
  target_include_directories(baresip PUBLIC include)
else()
  add_library(baresip SHARED ${SRCS} ${HEADERS})
  target_link_libraries(baresip PRIVATE ${LINKLIBS})
endif()

if(MSVC)
  set_target_properties(baresip PROPERTIES OUTPUT_NAME "libbaresip")
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


##############################################################################
#
# Install share
#

file(GLOB SHARE_FILES "${PROJECT_SOURCE_DIR}/share/*")
install(FILES ${SHARE_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/baresip)


##############################################################################
#
#

add_subdirectory(webrtc)
add_subdirectory(test)
