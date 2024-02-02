find_package(PkgConfig QUIET)
pkg_check_modules(PC_LIBRE QUIET libre)

find_path(RE_INCLUDE_DIR
  NAME re.h
  HINTS
    ../re/include
    ${PC_LIBRE_INCLUDEDIR}
    ${PC_LIBRE_INCLUDE_DIRS}
  PATHS /usr/local/include/re /usr/include/re
)

find_library(RE_LIBRARY
  NAMES re libre re-static
  HINTS
    ../re
    ../re/build
    ../re/build/Debug
    ${PC_LIBRE_LIBDIR}
    ${PC_LIBRE_LIBRARY_DIRS}
  PATHS /usr/local/lib64 /usr/lib64 /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RE DEFAULT_MSG RE_LIBRARY RE_INCLUDE_DIR)

mark_as_advanced(RE_INCLUDE_DIR RE_LIBRARY)

set(RE_INCLUDE_DIRS ${RE_INCLUDE_DIR})
set(RE_LIBRARIES ${RE_LIBRARY})
