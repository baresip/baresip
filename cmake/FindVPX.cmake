find_library(VPX_LIBRARY
  NAME vpx
  HINTS
    "${VPX_LIBRARY_DIRS}"
    "${VPX_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VPX DEFAULT_MSG VPX_LIBRARY)

if(VPX_FOUND)
  set( VPX_LIBRARIES ${VPX_LIBRARY} )
else()
  set( VPX_LIBRARIES )
endif()

mark_as_advanced( VPX_LIBRARIES )
