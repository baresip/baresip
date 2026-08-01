find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBRE QUIET libre)

find_path(RE_INCLUDE_DIR
  NAME re.h
  HINTS
    ../re/include
    ${PC_LIBRE_INCLUDEDIR}
    ${PC_LIBRE_INCLUDE_DIRS}
  PATHS /usr/local/include/re /usr/include/re
)

find_library(RE_LIBRARY
  NAMES re libre re-static
  HINTS
    ../re
    ../re/build
    ../re/build/Debug
    ${PC_LIBRE_LIBDIR}
    ${PC_LIBRE_LIBRARY_DIRS}
  PATHS /usr/local/lib64 /usr/lib64 /usr/local/lib /usr/lib
)

if("DATACHANNEL" IN_LIST RE_FIND_COMPONENTS)
  set(RE_DATACHANNEL_API_VERSION 1)
  set(RE_DATACHANNEL_FOUND FALSE)

  find_path(RE_DATACHANNEL_INCLUDE_DIR
    NAME re_datachannel.h
    HINTS
      ../re/include
      ${PC_LIBRE_INCLUDEDIR}
      ${PC_LIBRE_INCLUDE_DIRS}
    PATHS /usr/local/include/re /usr/include/re
  )

  find_library(RE_DATACHANNEL_LIBRARY
    NAMES re-datachannel
    HINTS
      ../re
      ../re/build
      ../re/build/Debug
      ${PC_LIBRE_LIBDIR}
      ${PC_LIBRE_LIBRARY_DIRS}
    PATHS /usr/local/lib64 /usr/lib64 /usr/local/lib /usr/lib
  )

  set(RE_DATACHANNEL_PRIVATE_LIBRARIES)
  if(RE_DATACHANNEL_LIBRARY MATCHES "${CMAKE_STATIC_LIBRARY_SUFFIX}$")
    find_package(USRSCTP REQUIRED)
    list(APPEND RE_DATACHANNEL_PRIVATE_LIBRARIES ${USRSCTP_LIBRARIES})
  endif()

  if(RE_DATACHANNEL_INCLUDE_DIR AND RE_DATACHANNEL_LIBRARY)
    include(CheckCSourceCompiles)
    set(_RE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES})
    set(_RE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
    set(CMAKE_REQUIRED_INCLUDES ${RE_DATACHANNEL_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES
      ${RE_DATACHANNEL_LIBRARY}
      ${RE_LIBRARY}
      ${RE_DATACHANNEL_PRIVATE_LIBRARIES}
    )
    unset(RE_DATACHANNEL_API_COMPATIBLE CACHE)
    check_c_source_compiles("
      #include <stdint.h>
      #include <re_datachannel.h>
      #if !defined(RE_DATACHANNEL_API_VERSION) || \
          RE_DATACHANNEL_API_VERSION != ${RE_DATACHANNEL_API_VERSION}
      #error incompatible re-datachannel API
      #endif
      int main(void) {
        return dc_api_version_1() == RE_DATACHANNEL_API_VERSION ? 0 : 1;
      }"
      RE_DATACHANNEL_API_COMPATIBLE)
    set(CMAKE_REQUIRED_INCLUDES ${_RE_REQUIRED_INCLUDES})
    set(CMAKE_REQUIRED_LIBRARIES ${_RE_REQUIRED_LIBRARIES})
    unset(_RE_REQUIRED_INCLUDES)
    unset(_RE_REQUIRED_LIBRARIES)
  endif()

  if(RE_DATACHANNEL_INCLUDE_DIR AND RE_DATACHANNEL_LIBRARY AND
     RE_DATACHANNEL_API_COMPATIBLE)
    set(RE_DATACHANNEL_FOUND TRUE)
    set(RE_DATACHANNEL_INCLUDE_DIRS ${RE_DATACHANNEL_INCLUDE_DIR})
    set(RE_DATACHANNEL_LIBRARIES
      ${RE_DATACHANNEL_LIBRARY}
      ${RE_LIBRARY}
      ${RE_DATACHANNEL_PRIVATE_LIBRARIES}
    )
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RE
  REQUIRED_VARS RE_LIBRARY RE_INCLUDE_DIR
  HANDLE_COMPONENTS
)

mark_as_advanced(
  RE_INCLUDE_DIR
  RE_LIBRARY
  RE_DATACHANNEL_INCLUDE_DIR
  RE_DATACHANNEL_LIBRARY
)

set(RE_INCLUDE_DIRS ${RE_INCLUDE_DIR})
set(RE_LIBRARIES ${RE_LIBRARY})
