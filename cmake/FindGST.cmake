find_path(GST_INCLUDE_DIR
  NAMES gst/gst.h
  HINTS
    "${GST_INCLUDE_DIRS}"
  PATHS /usr/include/gstreamer-1.0
)

find_path(GL_INCLUDE_DIR
  NAMES glib.h
  HINTS
    "${GL_INCLUDE_DIRS}"
  PATHS /usr/include/glib-2.0
)

find_path(GLIB_INCLUDE_DIR
  NAMES glibconfig.h
  HINTS
    "${GLIB_INCLUDE_DIRS}"
  PATHS /usr/lib/x86_64-linux-gnu/glib-2.0
        /usr/lib/x86_64-linux-gnu/glib-2.0/include
        /usr/lib64/glib-2.0
        /usr/lib/glib-2.0
)

find_library(GST_LIBRARY
  NAMES gstreamer-1.0
  HINTS
    "${GST_LIBRARY_DIRS}"
  PATHS /usr/lib/x86_64-linux-gnu
)

find_library(GL_LIBRARY
  NAMES glib-2.0
  HINTS
    "${GL_LIBRARY_DIRS}"
  PATHS /usr/lib/x86_64-linux-gnu
)

include(FindPackageHandleStandardArgs)

if(GST_INCLUDE_DIR AND GL_INCLUDE_DIR AND GLIB_INCLUDE_DIR)
  set(GST_INCLUDE_DIRS ${GST_INCLUDE_DIR} ${GL_INCLUDE_DIR} ${GLIB_INCLUDE_DIR})
  set(GST_LIBRARIES  ${GST_LIBRARY} ${GL_LIBRARY})
  set(GST_FOUND ON)
else()
  set(GST_INCLUDE_DIRS "")
  set(GST_LIBRARIES "")
  set(GST_FOUND OFF)
endif()

find_package_handle_standard_args(GST DEFAULT_MSG GST_FOUND)

mark_as_advanced(GST_LIBRARIES GST_INCLUDE_DIRS)
