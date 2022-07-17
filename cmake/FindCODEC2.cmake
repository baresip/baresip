# Find the system's codec2 includes and library
#
#  CODEC2_INCLUDE_DIRS - where to find codec2.h
#  CODEC2_LIBRARIES    - List of libraries when using codec2
#  CODEC2_FOUND        - True if codec2 found

if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(CODEC2 codec2)
endif()

find_path(CODEC2_INCLUDE_DIR
  NAMES codec2/codec2.h
  HINTS
    "${CODEC2_INCLUDE_DIRS}"
    "${CODEC2_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(CODEC2_LIBRARY
  NAMES codec2
  HINTS
    "${CODEC2_LIBRARY_DIRS}"
    "${CODEC2_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CODEC2 DEFAULT_MSG CODEC2_LIBRARY
    CODEC2_INCLUDE_DIR)

if(CODEC2_FOUND)
  set( CODEC2_INCLUDE_DIRS ${CODEC2_INCLUDE_DIR} )
  set( CODEC2_LIBRARIES ${CODEC2_LIBRARY} )
else()
  set( CODEC2_INCLUDE_DIRS )
  set( CODEC2_LIBRARIES )
endif()

mark_as_advanced( CODEC2_LIBRARIES CODEC2_INCLUDE_DIRS )
