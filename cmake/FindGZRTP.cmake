find_path(GZRTP_INCLUDE_DIR
    NAMES common/osSpecifics.h
    HINTS
        "${GZRTP_INCLUDE_DIRS}"
        "${GZRTP_HINTS}/include"
    PATHS ${CMAKE_SOURCE_DIR}/../ZRTPCPP
)

find_library(GZRTP_LIBRARY
    NAME zrtpcppcore
    HINTS
        "${GZRTP_LIBRARY_DIRS}"
        "${GZRTP_HINTS}/lib"
    PATHS ${CMAKE_SOURCE_DIR}/../ZRTPCPP/clients/no_client
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GZRTP DEFAULT_MSG GZRTP_LIBRARY
    GZRTP_INCLUDE_DIR)

if(GZRTP_FOUND)
  set( GZRTP_INCLUDE_DIRS ${GZRTP_INCLUDE_DIR} )
  list( APPEND GZRTP_INCLUDE_DIRS "${GZRTP_INCLUDE_DIR}/zrtp" )
  list( APPEND GZRTP_INCLUDE_DIRS "${GZRTP_INCLUDE_DIR}/srtp" )
  set( GZRTP_LIBRARIES ${GZRTP_LIBRARY} )
else()
  set( GZRTP_INCLUDE_DIRS )
  set( GZRTP_LIBRARIES )
endif()

mark_as_advanced( GZRTP_LIBRARIES GZRTP_INCLUDE_DIRS )
