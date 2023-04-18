# Find the system's ogg includes and library
#
#  OGG_INCLUDE_DIRS - where to find ogg.h
#  OGG_LIBRARIES    - List of libraries when using ogg
#  OGG_FOUND        - True if ogg found

if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(OGG ogg)
endif()

find_path(OGG_INCLUDE_DIR
  NAMES ogg/ogg.h
  HINTS
    "${OGG_INCLUDE_DIRS}"
    "${OGG_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(OGG_LIBRARY
  NAMES ogg
  HINTS
    "${OGG_LIBRARY_DIRS}"
    "${OGG_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OGG DEFAULT_MSG OGG_LIBRARY
    OGG_INCLUDE_DIR)

if(OGG_FOUND)
  set( OGG_INCLUDE_DIRS ${OGG_INCLUDE_DIR} )
  set( OGG_LIBRARIES ${OGG_LIBRARY} )
else()
  set( OGG_INCLUDE_DIRS )
  set( OGG_LIBRARIES )
endif()

mark_as_advanced( OGG_LIBRARIES OGG_INCLUDE_DIRS )
