# Find libgsm (GSM 06.10 audio codec)
# Headers may be in gsm.h or gsm/gsm.h depending on installation

find_path(GSM_INCLUDE_DIR_GSM_H
  NAMES gsm.h
  HINTS "${GSM_HINTS}/include"
  PATHS /usr/local/include /usr/include /usr/local/include/gsm /usr/include/gsm
)

find_path(GSM_INCLUDE_DIR_GSM_GSM_H
  NAMES gsm/gsm.h
  HINTS "${GSM_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

if(GSM_INCLUDE_DIR_GSM_GSM_H)
  set(GSM_INCLUDE_DIR ${GSM_INCLUDE_DIR_GSM_GSM_H})
  set(GSM_USE_GSM_GSM_H 1)
elseif(GSM_INCLUDE_DIR_GSM_H)
  set(GSM_INCLUDE_DIR ${GSM_INCLUDE_DIR_GSM_H})
  set(GSM_USE_GSM_GSM_H 0)
endif()

find_library(GSM_LIBRARY
  NAMES gsm
  HINTS "${GSM_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GSM DEFAULT_MSG GSM_LIBRARY GSM_INCLUDE_DIR)

if(GSM_FOUND)
  set(GSM_INCLUDE_DIRS ${GSM_INCLUDE_DIR})
  set(GSM_LIBRARIES ${GSM_LIBRARY})
else()
  set(GSM_INCLUDE_DIRS)
  set(GSM_LIBRARIES)
endif()

mark_as_advanced(GSM_LIBRARIES GSM_INCLUDE_DIRS)
