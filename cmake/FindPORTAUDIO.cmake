# Find the system's portaudio includes and library
#
#  PORTAUDIO_INCLUDE_DIRS - where to find portaudio.h
#  PORTAUDIO_LIBRARIES    - List of libraries when using portaudio
#  PORTAUDIO_FOUND        - True if portaudio found

if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(PORTAUDIO portaudio)
endif()

find_path(PORTAUDIO_INCLUDE_DIR
  NAMES portaudio.h
  HINTS
    "${PORTAUDIO_INCLUDE_DIRS}"
    "${PORTAUDIO_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(PORTAUDIO_LIBRARY
  NAMES portaudio
  HINTS
    "${PORTAUDIO_LIBRARY_DIRS}"
    "${PORTAUDIO_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PORTAUDIO DEFAULT_MSG PORTAUDIO_LIBRARY
    PORTAUDIO_INCLUDE_DIR)

if(PORTAUDIO_FOUND)
  set( PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIR} )
  set( PORTAUDIO_LIBRARIES ${PORTAUDIO_LIBRARY} )
else()
  set( PORTAUDIO_INCLUDE_DIRS )
  set( PORTAUDIO_LIBRARIES )
endif()

mark_as_advanced( PORTAUDIO_LIBRARIES PORTAUDIO_INCLUDE_DIRS )
