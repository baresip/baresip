# Find the system's ALSA includes and library
#
#  ALSA_INCLUDE_DIRS - where to find asoundlib.h
#  ALSA_LIBRARIES    - List of libraries when using asound
#  ALSA_FOUND        - True if asound found


if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(ALSA  alsa)
endif()

find_path(ALSA_INCLUDE_DIR
  NAMES asoundlib.h
  PATH_SUFFIXES alsa/
  HINTS
    ${ALSA_INCLUDE_DIRS}
    ${ALSA_HINTS}/include
  PATHS /usr/local/include /usr/include
)

find_library(ALSA_LIBRARY
  NAMES asound
  HINTS
    "${ALSA_LIBRARY_DIRS}"
    "${ALSA_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  ALSA DEFAULT_MSG
  ALSA_LIBRARY
  ALSA_INCLUDE_DIR)

if(ALSA_FOUND)
  set( ALSA_LIBRARIES ${ALSA_LIBRARY} )
  set( ALSA_INCLUDE_DIRS ${ALSA_INCLUDE_DIR} )
endif()

mark_as_advanced(ALSA_INCLUDE_DIR ALSA_LIBRARY)