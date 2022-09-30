# Find the system's GTK+ 3 includes and library
#
#
#  GTK3_CFLAGS(_OTHER) - where to find gtk.h
#  GTK3_LIBRARIES      - List of libraries when using GTK+ 3
#  GTK3_FOUND          - True if GTK+ 3 found

if(NOT WIN32)
  find_package(PkgConfig QUIET)
  pkg_check_modules(GTK3 gtk+-3.0)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GTK3 DEFAULT_MSG GTK3_LIBRARIES
    GTK3_CFLAGS GTK3_CFLAGS_OTHER)

mark_as_advanced( GTK3_LIBRARIES GTK3_CFLAGS GTK3_CFLAGS_OTHER )
