find_package(PkgConfig QUIET)
pkg_check_modules(GIO gio-unix-2.0)

find_path(GIO_INCLUDE_DIR
  NAMES gio/gio.h
  HINTS
    "${GIO_INCLUDE_DIRS}"
    "${GIO_HINTS}/include/gio-lib-2.0"
  PATHS /usr/local/include/glib-2.0 /usr/include/glib-2.0
)

find_library(GLIB_LIBRARY
  NAMES glib-2.0
  HINTS
    "${GIO_LIBRARY_DIRS}"
    "${GIO_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

find_library(GIO_LIBRARY
  NAMES gio-2.0
  HINTS
    "${GIO_LIBRARY_DIRS}"
    "${GIO_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

find_library(GOBJ_LIBRARY
  NAMES gobject-2.0
  HINTS
    "${GIO_LIBRARY_DIRS}"
    "${GIO_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GIO DEFAULT_MSG
  GLIB_LIBRARY GIO_LIBRARY GOBJ_LIBRARY GIO_INCLUDE_DIR)

if(GIO_FOUND)
  set( GIO_INCLUDE_DIRS ${GIO_INCLUDE_DIR} )
  set( GIO_LIBRARIES ${GLIB_LIBRARY} ${GIO_LIBRARY} ${GOBJ_LIBRARY} )
else()
  set( GIO_INCLUDE_DIRS )
  set( GIO_LIBRARIES )
endif()

mark_as_advanced( GIO_LIBRARIES GIO_INCLUDE_DIRS )
