find_path(WEBRTC_AECM_INCLUDE_DIR
    NAMES modules/audio_processing/aecm/echo_control_mobile.h
    HINTS
        "${WEBRTC_AECM_INCLUDE_DIRS}"
    PATHS ${CMAKE_SOURCE_DIR}/../webrtc/include
)

find_library(WEBRTC_AECM_LIBRARY
    NAME webrtc
    HINTS
        "${CMAKE_SOURCE_DIR}/../webrtc/obj/local/${CMAKE_ANDROID_ARCH_ABI}"
        "${WEBRTC_AECM_LIBRARY_DIRS}"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WEBRTC_AECM DEFAULT_MSG WEBRTC_AECM_LIBRARY
    WEBRTC_AECM_INCLUDE_DIR)

if(WEBRTC_AECM_FOUND)
  set( WEBRTC_AECM_INCLUDE_DIRS ${WEBRTC_AECM_INCLUDE_DIR} )
  set( WEBRTC_AECM_LIBRARIES ${WEBRTC_AECM_LIBRARY} )
else()
  set( WEBRTC_AECM_INCLUDE_DIRS )
  set( WEBRTC_AECM_LIBRARIES )
endif()

mark_as_advanced( WEBRTC_AECM_LIBRARIES WEBRTC_AECM_INCLUDE_DIRS )
