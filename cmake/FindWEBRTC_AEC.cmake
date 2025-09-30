find_path(WEBRTC_AEC_INCLUDE_DIR
    NAMES modules/audio_processing/include/audio_processing.h
    HINTS
        "${WEBRTC_AEC_INCLUDE_DIRS}"
    PATHS /usr/include/webrtc-audio-processing-1
)

find_library(WEBRTC_AEC_LIBRARY
    NAME webrtc-audio-processing-1
    HINTS
        "${WEBRTC_AEC_LIBRARY_DIRS}"
    PATHS /usr/lib/x86_64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WEBRTC_AEC DEFAULT_MSG WEBRTC_AEC_LIBRARY
    WEBRTC_AEC_INCLUDE_DIR)

if(WEBRTC_AEC_FOUND)
  set( WEBRTC_AEC_INCLUDE_DIRS ${WEBRTC_AEC_INCLUDE_DIR} )
  set( WEBRTC_AEC_LIBRARIES ${WEBRTC_AEC_LIBRARY} )
else()
  set( WEBRTC_AEC_INCLUDE_DIRS )
  set( WEBRTC_AEC_LIBRARIES )
endif()

mark_as_advanced( WEBRTC_AEC_LIBRARIES WEBRTC_AEC_INCLUDE_DIRS )
