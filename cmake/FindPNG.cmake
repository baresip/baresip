find_library(PNG_LIBRARY
  NAME png
  HINTS
    "${PNG_LIBRARY_DIRS}"
    "${PNG_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PNG DEFAULT_MSG PNG_LIBRARY)

if(PNG_FOUND)
  set( PNG_LIBRARIES ${PNG_LIBRARY} )
else()
  set( PNG_LIBRARIES )
endif()

mark_as_advanced( PNG_LIBRARIES )
