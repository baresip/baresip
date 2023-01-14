# Find the system's pulse includes and library
#
#  PULSE_INCLUDE_DIRS - where to find pulseaudio.h
#  PULSE_LIBRARIES    - List of libraries when using pulseaudio
#  PULSE_FOUND        - True if pulseaudio found

if(NOT WIN32)
    find_package(PkgConfig)
    pkg_search_module(PULSE pulse)
endif()

find_path(PULSE_INCLUDE_DIR
    NAMES pulse/pulseaudio.h
    HINTS
        "${PULSE_INCLUDE_DIRS}"
        "${PULSE_HINTS}/include"
    PATHS /usr/local/include /usr/include
)

find_library(PULSE_LIBRARY
    NAME pulse
    HINTS
        "${PULSE_LIBRARY_DIRS}"
        "${PULSE_HINTS}/lib"
    PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PULSE DEFAULT_MSG PULSE_LIBRARY
    PULSE_INCLUDE_DIR)

if(PULSE_FOUND)
    set( PULSE_INCLUDE_DIRS ${PULSE_INCLUDE_DIR} )
    set( PULSE_LIBRARIES ${PULSE_LIBRARY} )
else()
    set( PULSE_INCLUDE_DIRS )
    set( PULSE_LIBRARIES )
endif()

mark_as_advanced( PULSE_LIBRARIES PULSE_INCLUDE_DIRS )
