find_path(NB_INCLUDE_DIR
  NAMES opencore-amrnb amrnb
  HINTS
    "${AMR_INCLUDE_DIRS}"
    "${AMR_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_path(WB_ENC_INCLUDE_DIR
  NAMES opencore-amrwb/enc_if.h amrwb/enc_if.h vo-amrwbenc/enc_if.h
  HINTS
    "${AMR_INCLUDE_DIRS}"
    "${AMR_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_path(WB_DEC_INCLUDE_DIR
  NAMES opencore-amrwb/dec_if.h amrwb/dec_if.h
  HINTS
    "${AMR_INCLUDE_DIRS}"
    "${AMR_HINTS}/include"
  PATHS /usr/local/include /usr/include
)

find_library(NB_LIBRARY
  NAMES opencore-amrnb amrnb
  HINTS
    "${AMR_LIBRARY_DIRS}"
    "${AMR_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

find_library(WB_LIBRARY
  NAMES opencore-amrwb amrwb
  HINTS
    "${AMR_LIBRARY_DIRS}"
    "${AMR_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

find_library(WB_VO_ENC_LIBRARY
  NAME vo-amrwbenc
  HINTS
    "${AMR_LIBRARY_DIRS}"
    "${AMR_HINTS}/lib"
  PATHS /usr/local/lib /usr/lib
)

include(FindPackageHandleStandardArgs)

set( AMR_FOUND OFF )

if( NB_INCLUDE_DIR )
  set( AMR_INCLUDE_DIRS ${NB_INCLUDE_DIR}/opencore-amrnb
    ${NB_INCLUDE_DIR}/amrnb )
  set( AMR_LIBRARIES ${NB_LIBRARY} )
  set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMR_NB" )
  set( AMR_FOUND ON )
else()
  set( AMR_INCLUDE_DIRS "" )
  set( AMR_LIBRARIES "" )
endif()

if( WB_ENC_INCLUDE_DIR AND WB_DEC_INCLUDE_DIR )
  set( AMR_INCLUDE_DIRS ${AMR_INCLUDE_DIRS}
    ${WB_ENC_INCLUDE_DIR}/opencore-amrwb ${WB_ENC_INCLUDE_DIR}/amrwb
    ${WB_ENC_INCLUDE_DIR}/vo-amrwbenc
    ${WB_DEC_INCLUDE_DIR}/opencore-amrwb ${WB_DEC_INCLUDE_DIR}/amrwb )
  set( AMR_LIBRARIES ${AMR_LIBRARIES} ${WB_LIBRARY} ${WB_VO_ENC_LIBRARY} )
  set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMR_WB" )
  set( AMR_FOUND ON )
endif()

find_package_handle_standard_args(AMR DEFAULT_MSG AMR_FOUND)

mark_as_advanced( AMR_LIBRARIES AMR_INCLUDE_DIRS )
