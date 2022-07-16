
if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(AMR opencore-amrwb)
endif()

find_path(AMR_INCLUDE_DIR
  NAMES opencore-amrwb/dec_if.h
  HINTS
    "${AMR_INCLUDE_DIRS}"
    "${AMR_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(AMR_LIBRARY
  NAMES opencore-amrwb
  HINTS
    "${AMR_LIBRARY_DIRS}"
    "${AMR_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AMR DEFAULT_MSG AMR_LIBRARY
    AMR_INCLUDE_DIR)

if(AMR_FOUND)
  set( AMR_INCLUDE_DIRS ${AMR_INCLUDE_DIR} )
  set( AMR_LIBRARIES ${AMR_LIBRARY} )
else()
  set( AMR_INCLUDE_DIRS )
  set( AMR_LIBRARIES )
endif()

mark_as_advanced( AMR_LIBRARIES AMR_INCLUDE_DIRS )
