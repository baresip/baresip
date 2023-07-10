find_path(PNG_INCLUDE_DIR
  NAMES png.h
  HINTS
    "${PNG_INCLUDE_DIRS}"
    "${PNG_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(PNG_LIBRARY
  NAME png
  HINTS
    "${PNG_LIBRARY_DIRS}"
    "${PNG_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PNG DEFAULT_MSG PNG_LIBRARY
  PNG_INCLUDE_DIR)

if(PNG_FOUND)
  set( PNG_INCLUDE_DIRS ${PNG_INCLUDE_DIR} )
  set( PNG_LIBRARIES ${PNG_LIBRARY} )
else()
  set( PNG_INCLUDE_DIRS )
  set( PNG_LIBRARIES )
endif()

mark_as_advanced( PNG_INCLUDE_DIRS PNG_LIBRARIES )
