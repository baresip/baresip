find_package(PkgConfig)
pkg_check_modules(PKG_DIRECTFB QUIET directfb)

set(DIRECTFB_DEFINITIONS ${PKG_DIRECTFB_CFLAGS})

find_path(DIRECTFB_INCLUDE_DIR
  NAMES directfb.h
  HINTS ${PKG_DIRECTFB_INCLUDE_DIRS}
)

find_library(DIRECTFB_LIBRARIES
  NAMES directfb
  HINTS ${PKG_DIRECTFB_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DIRECTFB DEFAULT_MSG DIRECTFB_LIBRARIES
    DIRECTFB_INCLUDE_DIR)

mark_as_advanced(DIRECTFB_INCLUDE_DIR DIRECTFB_LIBRARIES)
