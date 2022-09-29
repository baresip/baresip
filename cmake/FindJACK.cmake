# Find the system's jack includes and library
#
#  JACK_INCLUDE_DIRS - where to find jack.h
#  JACK_LIBRARIES    - List of libraries when using jack
#  JACK_FOUND        - True if jack found

if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(JACK jack)
endif()

find_path(JACK_INCLUDE_DIR
  NAMES jack/jack.h
  HINTS
    "${JACK_INCLUDE_DIRS}"
    "${JACK_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(JACK_LIBRARY
  NAMES jack
  HINTS
    "${JACK_LIBRARY_DIRS}"
    "${JACK_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JACK DEFAULT_MSG JACK_LIBRARY
    JACK_INCLUDE_DIR)

if(JACK_FOUND)
  set( JACK_INCLUDE_DIRS ${JACK_INCLUDE_DIR} )
  set( JACK_LIBRARIES ${JACK_LIBRARY} )
else()
  set( JACK_INCLUDE_DIRS )
  set( JACK_LIBRARIES )
endif()

mark_as_advanced( JACK_LIBRARIES JACK_INCLUDE_DIRS )
