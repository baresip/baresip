# Find the system's zrtp include and library
#
#  ZRTP_INCLUDE_DIRS - where to find zrtp.h
#  ZRTP_LIBRARIES    - List of libraries when using zrtp
#  ZRTP_FOUND        - True if zrtp lib found
#  BN_FOUND          - True if bn lib found

if(NOT WIN32)
    find_package(PkgConfig)
    pkg_search_module(ZRTP zrtp)
endif()

find_path(ZRTP_INCLUDE_DIR
    NAMES libzrtp/zrtp.h
    HINTS
        "${ZRTP_INCLUDE_DIRS}"
        "${ZRTP_HINTS}/include"
    PATHS /usr/local/include /usr/include
)

find_library(ZRTP_LIBRARY
    NAME zrtp
    HINTS
        "${ZRTP_LIBRARY_DIRS}"
        "${ZRTP_HINTS}/lib"
    PATHS /usr/local/lib usr/lib
)

find_library(BN_LIBRARY
    NAME bn
    HINTS
        "${ZRTP_LIBRARY_DIRS}"
        "${ZRTP_HINTS}/lib"
    PATHS /usr/local/lib usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZRTP DEFAULT_MSG ZRTP_LIBRARY
    ZRTP_INCLUDE_DIR)

if(ZRTP_FOUND)
    set( ZRTP_INCLUDE_DIRS ${ZRTP_INCLUDE_DIR}/libzrtp )
    set( ZRTP_LIBRARIES ${ZRTP_LIBRARY} ${BN_LIBRARY} )
else()
    set( ZRTP_INCLUDE_DIRS )
    set( ZRTP_LIBRARIES )
endif()

mark_as_advanced( ZRTP_LIBRARIES ZRTP_INCLUDE_DIRS )
