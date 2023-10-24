find_path(FVAD_INCLUDE_DIR
  NAMES fvad.h
  HINTS
    "${FVAD_INCLUDE_DIRS}"
    "${FVAD_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(FVAD_LIBRARY
  NAME libfvad.a
  HINTS
    "${FVAD_LIBRARY_DIRS}"
    "${FVAD_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FVAD DEFAULT_MSG FVAD_LIBRARY
  FVAD_INCLUDE_DIR)

if(FVAD_FOUND)
  set( FVAD_INCLUDE_DIRS ${FVAD_INCLUDE_DIR} )
  set( FVAD_LIBRARIES ${FVAD_LIBRARY} )
else()
  set( FVAD_INCLUDE_DIRS )
  set( FVAD_LIBRARIES )
endif()

mark_as_advanced( FVAD_INCLUDE_DIRS FVAD_LIBRARIES )
