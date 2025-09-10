#
# CMakeLists.txt
#
# Copyright (C) 2010 - 2025 Alfred E. Heggestad
# Copyright (C) 2022 - 2025 Sebastian Reimers
# Copyright (C) 2023 - 2025 Christian Spielberger
#

##############################################################################
#
# Versioning
#

cmake_minimum_required(VERSION 3.18...4.0)

project(baresip VERSION 4.1.0)

set(PROJECT_SOVERSION 22) # bump if ABI breaks

# Pre-release identifier, comment out on a release
# Increment for breaking changes (dev2, dev3...)
#set(PROJECT_VERSION_PRE dev)

if(PROJECT_VERSION_PRE)
  set(PROJECT_VERSION_FULL ${PROJECT_VERSION}-${PROJECT_VERSION_PRE})
else()
  set(PROJECT_VERSION_FULL ${PROJECT_VERSION})
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

##############################################################################
#
# Module/Package Includes
#

include(GNUInstallDirs)
include(CheckIncludeFile)
find_package(RE REQUIRED)

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

if(APPLE)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

if(MSVC)
  add_compile_options("/W3")
else()
  add_compile_options(
    -Wall
    -Wextra
  )

  set(c_flags
    -pedantic
    -Wcast-align
    -Wbad-function-cast
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

  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND c_flags
      -Watomic-implicit-seq-cst
      -Wshorten-64-to-32
      -Wno-gnu-zero-variadic-macro-arguments
      -Wno-c2x-extensions
    )
  endif()

  add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:${c_flags}>"
  )
endif()



find_package(re CONFIG REQUIRED HINTS ../re/cmake)

list(APPEND RE_DEFINITIONS
  VERSION="${PROJECT_VERSION_FULL}"
  VER_MAJOR=${PROJECT_VERSION_MAJOR}
  VER_MINOR=${PROJECT_VERSION_MINOR}
  VER_PATCH=${PROJECT_VERSION_PATCH}
  _GNU_SOURCE
)

add_compile_definitions(${RE_DEFINITIONS})

include_directories(
  include
  src
  ${RE_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
)

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

if(DEFAULT_CAPATH)
  add_definitions(-DDEFAULT_CAPATH="${DEFAULT_CAPATH}")
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
  src/aureceiver.c
  src/ausrc.c
  src/baresip.c
  src/bevent.c
  src/bundle.c
  src/call.c
  src/cmd.c
  src/conf.c
  src/config.c
  src/contact.c
  src/custom_hdrs.c
  src/descr.c
  src/dial_number.c
  src/http.c
  src/jbuf.c
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
  src/rtprecv.c
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

set(MODULES_DETECTED "")

foreach(mod IN LISTS MODULES)
  add_subdirectory(modules/${mod})
endforeach()

foreach(mod IN LISTS APP_MODULES)
  add_subdirectory(
    ${APP_MODULES_DIR}/${mod}
    ${CMAKE_CURRENT_BINARY_DIR}/app_modules/${mod}
  )
  list(APPEND MODULES_DETECTED ${mod})
endforeach()

list(REMOVE_DUPLICATES MODULES_DETECTED)

foreach(mod IN LISTS MODULES_DETECTED)
  set_target_properties(${mod} PROPERTIES PREFIX "")
endforeach()

message("MODULES_DETECTED=${MODULES_DETECTED}")

if(STATIC)
  foreach(mod IN LISTS MODULES_DETECTED)
    set(MOD_EXPORTS
      "${MOD_EXPORTS}extern const struct mod_export exports_${mod};\n")
    set(MOD_EXPORT_TABLE
      "${MOD_EXPORT_TABLE}  &exports_${mod},\n")
  endforeach()

  configure_file(src/static.c.in src/static.c)
  list(APPEND SRCS ${CMAKE_CURRENT_BINARY_DIR}/src/static.c)
else()
  foreach(mod IN LISTS MODULES_DETECTED)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      set_target_properties(${mod} PROPERTIES
                            LINK_FLAGS "-undefined dynamic_lookup")
    endif()
  endforeach()
endif()


##############################################################################
#
# Main target library
#

set(LINKLIBS ${RE_LIBRARIES} ${RE_LIBS})

if(WIN32)
  list(APPEND LINKLIBS winmm gdi32 crypt32 strmiids ole32 oleaut32)
endif()

if(STATIC)
  add_library(baresip STATIC ${SRCS} ${HEADERS})
  target_link_libraries(baresip PUBLIC ${LINKLIBS} ${MODULES_DETECTED})
  target_include_directories(baresip PUBLIC include)
else()
  add_library(baresip SHARED ${SRCS} ${HEADERS})
  target_link_libraries(baresip PRIVATE ${LINKLIBS})
  set_target_properties(baresip PROPERTIES VERSION
    ${PROJECT_SOVERSION}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})
  set_target_properties(baresip PROPERTIES SOVERSION ${PROJECT_SOVERSION})
endif()

if(MSVC)
  set_target_properties(baresip PROPERTIES OUTPUT_NAME "libbaresip")
endif()

set_target_properties(baresip PROPERTIES POSITION_INDEPENDENT_CODE ON)

set_target_properties(baresip PROPERTIES PUBLIC_HEADER "${HEADERS}")


##############################################################################
#
# Target baresip executable
#

add_executable(baresip_exe src/main.c)

set_target_properties(baresip_exe PROPERTIES OUTPUT_NAME baresip)

target_link_libraries(baresip_exe PRIVATE baresip ${LINKLIBS})


##############################################################################
#
# PKGCONF section
#

configure_file(packaging/libbaresip.pc.in libbaresip.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libbaresip.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
  COMPONENT Development
)


##############################################################################
# Test and webrtc
#

add_subdirectory(webrtc)
add_subdirectory(test)


##############################################################################
# Symlink modules to build dir (needed for test and baresip_exe)
#

if(NOT STATIC AND UNIX)
  foreach(mod IN LISTS MODULES_DETECTED)
    set(mod_lib ${mod}${CMAKE_SHARED_MODULE_SUFFIX})
    add_custom_command(
      TARGET baresip_exe POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_BINARY_DIR}/modules/${mod}/${mod_lib}
        ${CMAKE_CURRENT_BINARY_DIR}/${mod_lib}
    )
  endforeach()
  foreach(mod IN LISTS APP_MODULES)
    set(mod_lib ${mod}${CMAKE_SHARED_MODULE_SUFFIX})
    add_custom_command(
      TARGET baresip_exe POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_BINARY_DIR}/app_modules/${mod}/${mod_lib}
        ${CMAKE_CURRENT_BINARY_DIR}/${mod_lib}
    )
  endforeach()
endif()


##############################################################################
#
# Install section
#

install(TARGETS baresip_exe baresip
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Applications
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Libraries
    NAMELINK_SKIP
  ARCHIVE
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Development
  PUBLIC_HEADER
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT Development
)

if(NOT STATIC)
  foreach(mod IN LISTS MODULES_DETECTED)
    install(TARGETS ${mod}
      LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/baresip/modules
        COMPONENT Applications
    )
  endforeach()
endif()

install(TARGETS baresip
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    NAMELINK_ONLY
    COMPONENT Development
)

file(GLOB SHARE_FILES
  "${PROJECT_SOURCE_DIR}/share/*.png"
  "${PROJECT_SOURCE_DIR}/share/*.wav"
)
file(GLOB DESKTOP_FILES "${PROJECT_SOURCE_DIR}/share/*.desktop")

install(FILES ${SHARE_FILES}
  DESTINATION ${CMAKE_INSTALL_DATADIR}/baresip
  COMPONENT Applications
)

install(FILES ${DESKTOP_FILES}
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  COMPONENT Applications
)


##############################################################################
#
# Packaging section
#

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(packaging)
endif()
