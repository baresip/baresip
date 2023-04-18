# Find the system's speex includes and library
#
#  SPEEX_FOUND       - True if speex found

if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(SPEEX speex)
endif()

find_path(SPEEX_INCLUDE_DIR
  NAMES speex/speex.h
  HINTS
    "${SPEEX_INCLUDE_DIRS}"
    "${SPEEX_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(SPEEX_LIBRARY
  NAMES speex
  HINTS
    "${SPEEX_LIBRARY_DIRS}"
    "${SPEEX_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SPEEX DEFAULT_MSG SPEEX_LIBRARY
    SPEEX_INCLUDE_DIR)

if(SPEEX_FOUND)
  set( SPEEX_INCLUDE_DIRS ${SPEEX_INCLUDE_DIR} )
  set( SPEEX_LIBRARIES ${SPEEX_LIBRARY} )
else()
  set( SPEEX_INCLUDE_DIRS )
  set( SPEEX_LIBRARIES )
endif()

mark_as_advanced( SPEEX_LIBRARIES SPEEX_INCLUDE_DIRS )
