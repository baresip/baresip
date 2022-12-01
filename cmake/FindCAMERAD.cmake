find_path(CAMERAD_INCLUDE_DIR
  NAMES cameradclient/cameradclient.h
  HINTS
    "${CAMERAD_INCLUDE_DIRS}"
)

find_library(CAMERAD_LIBRARY
  NAMES cameradclient
  HINTS
    "${CAMERAD_LIBRARY_DIRS}"
)


include(FindPackageHandleStandardArgs)

if(CAMERAD_INCLUDE_DIR)
  set(CAMERAD_INCLUDE_DIRS ${CAMERAD_INCLUDE_DIR})
  set(CAMERAD_LIBRARIES  ${CAMERAD_LIBRARY})
  set(CAMERAD_FOUND ON)
else()
  set(CAMERAD_INCLUDE_DIRS "")
  set(CAMERAD_LIBRARIES "")
  set(CAMERAD_FOUND OFF)
endif()

find_package_handle_standard_args(CAMERAD DEFAULT_MSG CAMERAD_FOUND)

mark_as_advanced(CAMERAD_LIBRARIES CAMERAD_INCLUDE_DIRS)
