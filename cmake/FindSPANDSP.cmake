find_path(SPANDSP_INCLUDE_DIR
  NAME spandsp/g722.h
  HINTS
    "${SPANDSP_INCLUDE_DIRS}"
    "${SPANDSP_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(SPANDSP_LIBRARY
  NAME spandsp
  HINTS
    "${SPANDSP_LIBRARY_DIRS}"
    "${SPANDSP_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SPANDSP DEFAULT_MSG SPANDSP_LIBRARY
    SPANDSP_INCLUDE_DIR)

if(SPANDSP_FOUND)
  set( SPANDSP_INCLUDE_DIRS ${SPANDSP_INCLUDE_DIR} )
  set( SPANDSP_LIBRARIES ${SPANDSP_LIBRARY} )
else()
  set( SPANDSP_INCLUDE_DIRS )
  set( SPANDSP_LIBRARIES )
endif()

mark_as_advanced( SPANDSP_LIBRARIES SPANDSP_INCLUDE_DIRS )
