find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBREM QUIET librem)

find_path(REM_INCLUDE_DIR rem.h
  HINTS ../rem/include ${PC_LIBREM_INCLUDEDIR} ${PC_LIBREM_INCLUDE_DIRS})

find_library(REM_LIBRARY NAMES rem librem rem-static
  HINTS ../rem ../rem/build ../rem/build/Debug
  ${PC_LIBREM_LIBDIR} ${PC_LIBREM_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(REM DEFAULT_MSG REM_LIBRARY REM_INCLUDE_DIR)

mark_as_advanced(REM_INCLUDE_DIR REM_LIBRARY)

set(REM_INCLUDE_DIRS ${REM_INCLUDE_DIR})
set(REM_LIBRARIES ${REM_LIBRARY})
