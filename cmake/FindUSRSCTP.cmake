# Find usrsctp.
#
# Defines:
#   USRSCTP_FOUND
#   USRSCTP_INCLUDE_DIRS
#   USRSCTP_LIBRARIES

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_USRSCTP QUIET usrsctp)
endif()

find_path(USRSCTP_INCLUDE_DIR
  NAMES usrsctp.h
  HINTS ${PC_USRSCTP_INCLUDE_DIRS}
)

find_library(USRSCTP_LIBRARY
  NAMES usrsctp
  HINTS ${PC_USRSCTP_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(USRSCTP
  REQUIRED_VARS USRSCTP_INCLUDE_DIR USRSCTP_LIBRARY
)

set(USRSCTP_INCLUDE_DIRS ${USRSCTP_INCLUDE_DIR})
set(USRSCTP_LIBRARIES ${USRSCTP_LIBRARY})
mark_as_advanced(USRSCTP_INCLUDE_DIR USRSCTP_LIBRARY)
