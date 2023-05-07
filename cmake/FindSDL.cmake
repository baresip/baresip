# Find the system's SDL2 includes and library
#
#  SDL_INCLUDE_DIRS - where to find SDL.h
#  SDL_LIBRARIES    - List of libraries when using SDL2
#  SDL_FOUND        - True if SDL2 found

if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(SDL sdl2)
endif()

find_path(SDL_INCLUDE_DIR
  NAMES SDL2/SDL.h SDL.h
  HINTS
    "${SDL_INCLUDE_DIRS}"
    "${SDL_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(SDL_LIBRARY
  NAMES SDL2
  HINTS
    "${SDL_LIBRARY_DIRS}"
    "${SDL_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL DEFAULT_MSG SDL_LIBRARY
    SDL_INCLUDE_DIR)

if(SDL_FOUND)
  set( SDL_INCLUDE_DIRS ${SDL_INCLUDE_DIR} )
  set( SDL_LIBRARIES ${SDL_LIBRARY} )
else()
  set( SDL_INCLUDE_DIRS )
  set( SDL_LIBRARIES )
endif()

mark_as_advanced( SDL_LIBRARIES SDL_INCLUDE_DIRS )
