# Find the system's pipewire includes and library
#
#  PIPEWIRE_INCLUDE_DIRS - where to find pipewire.h
#  PIPEWIRE_LIBRARIES    - List of libraries when using pipewire
#  PIPEWIRE_FOUND        - True if pipewire found

if(NOT WIN32)
    find_package(PkgConfig)
    pkg_search_module(PIPEWIRE libpipewire-0.3)
endif()
