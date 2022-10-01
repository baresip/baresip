# Find the system's twolame, (mp3)lame, mpg123 and speexdsp includes and
# libraries
#
#  MPA_INCLUDE_DIRS - where to find twolame.h, lame/lame.h, mpg123.h and
#                     speex/speex_resampler.h
#  MPA_LIBRARIES    - List of libraries when using twolame, mp3lame, mpg123
#                     and speexdsp
#  MPA_FOUND        - True if twolame, mp3lame, mpg123 and speexdsp found

# twolame
if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(TWOLAME twolame)
endif()

find_path(TWOLAME_INCLUDE_DIR
  NAMES twolame.h
  HINTS
    "${TWOLAME_INCLUDE_DIRS}"
    "${TWOLAME_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(TWOLAME_LIBRARY
  NAMES twolame
  HINTS
    "${TWOLAME_LIBRARY_DIRS}"
    "${TWOLAME_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

# (mp3)lame
find_path(MP3LAME_INCLUDE_DIR
  NAMES lame/lame.h
  HINTS
    "${MP3LAME_INCLUDE_DIRS}"
    "${MP3LAME_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(MP3LAME_LIBRARY
  NAMES mp3lame
  HINTS
    "${MP3LAME_LIBRARY_DIRS}"
    "${MP3LAME_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

# mpg123
if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(MPG123 mpg123)
endif()

find_path(MPG123_INCLUDE_DIR
  NAMES mpg123.h
  HINTS
    "${MPG123_INCLUDE_DIRS}"
    "${MPG123_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(MPG123_LIBRARY
  NAMES mpg123
  HINTS
    "${MPG123_LIBRARY_DIRS}"
    "${MPG123_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

# speexdsp
if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(SPEEXDSP speexdsp)
endif()

find_path(SPEEXDSP_INCLUDE_DIR
  NAMES speex/speex_resampler.h
  HINTS
    "${SPEEXDSP_INCLUDE_DIRS}"
    "${SPEEXDSP_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(SPEEXDSP_LIBRARY
  NAMES speexdsp
  HINTS
    "${SPEEXDSP_LIBRARY_DIRS}"
    "${SPEEXDSP_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

# Common
if(TWOLAME_INCLUDE_DIR AND MP3LAME_INCLUDE_DIR AND MPG123_INCLUDE_DIR
  AND SPEEXDSP_INCLUDE_DIR)
  set(MPA_INCLUDE_DIRS
    ${TWOLAME_INCLUDE_DIR}
    ${MP3LAME_INCLUDE_DIR}
    ${MPG123_INCLUDE_DIR}
    ${SPEEXDSP_INCLUDE_DIR}
  )
  set(MPA_LIBRARIES
    ${TWOLAME_LIBRARY}
    ${MP3LAME_LIBRARY}
    ${MPG123_LIBRARY}
    ${SPEEXDSP_LIBRARY}
  )
  set(MPA_FOUND ON)
else()
  set(MPA_INCLUDE_DIRS)
  set(MPA_LIBRARIES)
  set(MPA_FOUND OFF)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPA DEFAULT_MSG MPA_LIBRARIES
  MPA_INCLUDE_DIRS)

mark_as_advanced(MPA_LIBRARIES MPA_INCLUDE_DIRS)
