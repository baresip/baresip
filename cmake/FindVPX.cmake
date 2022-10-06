find_path(VPX_INCLUDE_DIR
  NAMES vpx/vpx_codec.h
  HINTS
    "${VPX_INCLUDE_DIRS}"
    "${VPX_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(VPX_LIBRARY
  NAME vpx
  HINTS
    "${VPX_LIBRARY_DIRS}"
    "${VPX_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VPX DEFAULT_MSG VPX_LIBRARY
  VPX_INCLUDE_DIR)

if(VPX_FOUND)
  set( VPX_INCLUDE_DIRS ${VPX_INCLUDE_DIR} )
  set( VPX_LIBRARIES ${VPX_LIBRARY} )
else()
  set( VPX_INCLUDE_DIRS )
  set( VPX_LIBRARIES )
endif()

mark_as_advanced( VPX_INCLUDE_DIRS VPX_LIBRARIES )
