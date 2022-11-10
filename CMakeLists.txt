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

project(baresip VERSION 2.9.0)

set(PROJECT_SOVERSION 1) # bump if ABI breaks

# Pre-release identifier, comment out on a release
# Increment for breaking changes (dev2, dev3...)
set(PROJECT_VERSION_PRE dev)

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
      list(APPEND c_flags -Watomic-implicit-seq-cst -Wshorten-64-to-32)
  endif()

  add_compile_options(
    "$<$<COMPILE_LANGUAGE:C>:${c_flags}>"
  )
endif()



find_package(re CONFIG REQUIRED HINTS ../re/cmake)

list(APPEND RE_DEFINITIONS
  -DVERSION="${PROJECT_VERSION_FULL}"
  -DVER_MAJOR=${PROJECT_VERSION_MAJOR}
  -DVER_MINOR=${PROJECT_VERSION_MINOR}
  -DVER_PATCH=${PROJECT_VERSION_PATCH}
)

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
# Packaging section
#

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(packaging)
endif()

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
  foreach(mod IN LISTS MODULES)
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
  foreach(mod IN LISTS MODULES)
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

file(GLOB SHARE_FILES "${PROJECT_SOURCE_DIR}/share/*")

install(FILES ${SHARE_FILES}
  DESTINATION ${CMAKE_INSTALL_DATADIR}/baresip
  COMPONENT Applications
)
