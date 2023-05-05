# Find the system's opus includes and library
#
#  OPUS_INCLUDE_DIRS - where to find opus.h
#  OPUS_LIBRARIES    - List of libraries when using opus
#  OPUS_FOUND        - True if opus found

if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(OPUS opus)
endif()

find_path(OPUS_INCLUDE_DIR
  NAMES opus/opus.h opus.h
  HINTS
    "${OPUS_INCLUDE_DIRS}"
    "${OPUS_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(OPUS_LIBRARY
  NAMES opus
  HINTS
    "${OPUS_LIBRARY_DIRS}"
    "${OPUS_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OPUS DEFAULT_MSG OPUS_LIBRARY
    OPUS_INCLUDE_DIR)

if(OPUS_FOUND)
  set( OPUS_INCLUDE_DIRS ${OPUS_INCLUDE_DIR} )
  set( OPUS_LIBRARIES ${OPUS_LIBRARY} )
else()
  set( OPUS_INCLUDE_DIRS )
  set( OPUS_LIBRARIES )
endif()

mark_as_advanced( OPUS_LIBRARIES OPUS_INCLUDE_DIRS )
