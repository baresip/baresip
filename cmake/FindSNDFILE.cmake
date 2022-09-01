
if( SNDFILE_INCLUDE_DIR AND SNDFILE_LIBRARIES )
  set(Sndfile_FIND_QUIETLY TRUE)
endif()

if(NOT WIN32)
  pkg_check_modules(_pc_SNDFILE sndfile)
endif()

find_path(SNDFILE_INCLUDE_DIR
  NAMES sndfile.h
  HINTS ${_pc_SNDFILE_INCLUDE_DIRS}
)

find_library(SNDFILE_LIBRARIES
  NAMES sndfile
  HINTS ${_pc_SNDFILE_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SNDFILE DEFAULT_MSG
    SNDFILE_INCLUDE_DIR SNDFILE_LIBRARIES)

mark_as_advanced(SNDFILE_INCLUDE_DIR SNDFILE_LIBRARIES)
