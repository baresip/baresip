
if(NOT WIN32)
  find_package(PkgConfig)
  pkg_search_module(APTX aptx)
endif()

find_path(APTX_INCLUDE_DIR
  NAMES openaptx.h
  HINTS
    "${APTX_INCLUDE_DIRS}"
    "${APTX_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(APTX_LIBRARY
  NAMES openaptx
  HINTS
    "${APTX_LIBRARY_DIRS}"
    "${APTX_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(APTX DEFAULT_MSG APTX_LIBRARY
    APTX_INCLUDE_DIR)

if(APTX_FOUND)
  set( APTX_INCLUDE_DIRS ${APTX_INCLUDE_DIR} )
  set( APTX_LIBRARIES ${APTX_LIBRARY} )
else()
  set( APTX_INCLUDE_DIRS )
  set( APTX_LIBRARIES )
endif()

mark_as_advanced( APTX_LIBRARIES APTX_INCLUDE_DIRS )
