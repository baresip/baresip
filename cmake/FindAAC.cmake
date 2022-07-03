find_package(PkgConfig QUIET)
pkg_search_module(AAC fdk-aac)

find_path(AAC_INCLUDE_DIR
  NAMES fdk-aac/FDK_audio.h
  HINTS
    "${AAC_INCLUDE_DIRS}"
    "${AAC_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(AAC_LIBRARY
  NAMES fdk-aac
  HINTS
    "${AAC_LIBRARY_DIRS}"
    "${AAC_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AAC DEFAULT_MSG AAC_LIBRARY
    AAC_INCLUDE_DIR)

if(AAC_FOUND)
  set( AAC_INCLUDE_DIRS ${AAC_INCLUDE_DIR} )
  set( AAC_LIBRARIES ${AAC_LIBRARY} )
else()
  set( AAC_INCLUDE_DIRS )
  set( AAC_LIBRARIES )
endif()

mark_as_advanced( AAC_LIBRARIES AAC_INCLUDE_DIRS )
