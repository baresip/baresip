find_package(PkgConfig QUIET)
pkg_check_modules(GLIB glib-2.0 gio-unix-2.0)

mark_as_advanced( GLIB_LIBRARIES GLIB_INCLUDE_DIRS )
