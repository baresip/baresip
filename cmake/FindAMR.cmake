find_path(AMR_INCLUDE_DIR
  NAMES opencore-amrwb/dec_if.h opencore-amrnb/interf_enc.h
  HINTS
    "${AMR_INCLUDE_DIRS}"
    "${AMR_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(AMR_LIBRARY
  NAMES opencore-amrwb opencore-amrnb
  HINTS
    "${AMR_LIBRARY_DIRS}"
    "${AMR_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

find_path(NB_INCLUDE_DIR
  NAME opencore-amrnb/interf_enc.h
  HINTS
    "${AMR_INCLUDE_DIRS}"
    "${AMR_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_path(WB_INCLUDE_DIR
  NAME opencore-amrwb/dec_if.h
  HINTS
    "${AMR_INCLUDE_DIRS}"
    "${AMR_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(NB_LIBRARY
  NAME opencore-amrnb
  HINTS
    "${AMR_LIBRARY_DIRS}"
    "${AMR_HINTS}/lib"
  PATHS /usr/local/lib usr/lib
)

find_library(WB_LIBRARY
  NAME opencore-amrwb
  HINTS
    "${AMR_LIBRARY_DIRS}"
    "${AMR_HINTS}/lib"
  PATHS /usr/local/lib usr/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AMR DEFAULT_MSG AMR_LIBRARY
    AMR_INCLUDE_DIR)

if(AMR_FOUND)
  if ( NB_INCLUDE_DIR )
    set( AMR_INCLUDE_DIRS ${NB_INCLUDE_DIR}/opencore-amrnb )
    set( AMR_LIBRARIES ${NB_LIBRARY} )
    set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMR_NB" )
  else()
    set( AMR_INCLUDE_DIRS "" )
    set( AMR_LIBRARIES "" )
  endif()
  if ( WB_INCLUDE_DIR )
    set( AMR_INCLUDE_DIRS ${AMR_INCLUDE_DIRS} ${WB_INCLUDE_DIR}/opencore-amrwb )
    set( AMR_LIBRARIES ${AMR_LIBRARIES} ${WB_LIBRARY} )
    set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMR_WB" )
  endif()
endif()

mark_as_advanced( AMR_LIBRARIES AMR_INCLUDE_DIRS )
