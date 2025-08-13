# First try to use the CMake variables set by contrib.mk
if(DEFINED G722_INCLUDE_DIR AND DEFINED G722_LIBRARY)
  set(LIBG722_INCLUDE_DIR ${G722_INCLUDE_DIR})
  set(LIBG722_LIBRARY ${G722_LIBRARY})
  set(LIBG722_FOUND TRUE)
else()
  # Fallback to searching in standard locations
  find_path(LIBG722_INCLUDE_DIR
    NAME g722_codec.h
    HINTS
      "${LIBG722_INCLUDE_DIRS}"
      "${LIBG722_HINTS}/include"
    PATHS /usr/local/include /usr/include
  )

  find_library(LIBG722_LIBRARY
    NAME g722
    HINTS
      "${LIBG722_LIBRARY_DIRS}"
      "${LIBG722_HINTS}/lib"
    PATHS /usr/local/lib /usr/lib
  )
endif()

# Only use find_package_handle_standard_args if we haven't already found the library
if(NOT DEFINED LIBG722_FOUND)
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(LIBG722 DEFAULT_MSG LIBG722_LIBRARY
      LIBG722_INCLUDE_DIR)
endif()

if(LIBG722_FOUND)
  set( LIBG722_INCLUDE_DIRS ${LIBG722_INCLUDE_DIR} )
  set( LIBG722_LIBRARIES ${LIBG722_LIBRARY} )
else()
  set( LIBG722_INCLUDE_DIRS )
  set( LIBG722_LIBRARIES )
endif()

mark_as_advanced( LIBG722_LIBRARIES LIBG722_INCLUDE_DIRS )
