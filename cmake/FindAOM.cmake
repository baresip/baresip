
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_AOM aom QUIET)
endif()

find_path(AOM_INCLUDE_DIR
  NAMES aom/aom.h
  PATHS ${PC_AOM_INCLUDEDIR}
)

find_library(AOM_LIBRARY
  NAMES aom libaomf
  PATHS ${PC_AOM_LIBDIR}
)

set(AOM_VERSION ${PC_AOM_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AOM
                                  REQUIRED_VARS AOM_LIBRARY AOM_INCLUDE_DIR
                                  VERSION_VAR AOM_VERSION)

if(AOM_FOUND)
  set(AOM_LIBRARIES ${AOM_LIBRARY})

  if(NOT TARGET AOM::AOM)
    add_library(AOM::AOM UNKNOWN IMPORTED)
    set_target_properties(AOM::AOM PROPERTIES
                          IMPORTED_LOCATION "${AOM_LIBRARY}")
  endif()
endif()

mark_as_advanced(AOM_INCLUDE_DIR AOM_LIBRARY)
