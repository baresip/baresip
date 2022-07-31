
find_package(PkgConfig)
pkg_search_module(G7221 g7221)

find_path(G7221_INCLUDE_DIR
  NAMES g722_1/g722_1.h
  HINTS
    "${G7221_INCLUDE_DIRS}"
    "${G7221_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(G7221_LIBRARY
  NAMES g722_1
  HINTS
    "${G7221_LIBRARY_DIRS}"
    "${G7221_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(G7221 DEFAULT_MSG G7221_LIBRARY
    G7221_INCLUDE_DIR)

if(G7221_FOUND)
  set( G7221_INCLUDE_DIRS ${G7221_INCLUDE_DIR} )
  set( G7221_LIBRARIES ${G7221_LIBRARY} )
else()
  set( G7221_INCLUDE_DIRS )
  set( G7221_LIBRARIES )
endif()

mark_as_advanced( G7221_LIBRARIES G7221_INCLUDE_DIRS )
