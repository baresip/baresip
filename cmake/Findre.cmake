find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBRE QUIET libre)

find_path(RE_INCLUDE_DIR re.h
  HINTS ../re/include ${PC_LIBRE_INCLUDEDIR} ${PC_LIBRE_INCLUDE_DIRS})

find_library(RE_LIBRARY NAMES re libre
  HINTS ../re ../re/build ${PC_LIBRE_LIBDIR} ${PC_LIBRE_LIBRARY_DIRS})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(re DEFAULT_MSG RE_LIBRARY RE_INCLUDE_DIR)

mark_as_advanced(RE_INCLUDE_DIR RE_LIBRARY)

set(RE_INCLUDE_DIRS ${RE_INCLUDE_DIR})
set(RE_LIBRARIES ${RE_LIBRARY})
