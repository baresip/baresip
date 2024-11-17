find_path(AAUDIO_INCLUDE_DIR
    NAMES aaudio/AAudio.h
    PATHS "${TOOLCHAIN}/sysroot/usr/include"
)

# TARGET examples: aarch64-linux-android, arm-linux-androideabi,
# x86_64-linux-android

find_library(AAUDIO_LIBRARY
    NAME aaudio
    HINTS "${TOOLCHAIN}/sysroot/usr/lib/${CMAKE_ANDROID_ARCH_ABI}/"
    "${ANDROID_PLATFORM}"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(AAUDIO DEFAULT_MSG AAUDIO_LIBRARY
    AAUDIO_INCLUDE_DIR)

if(AAUDIO_FOUND)
    message(STATUS "aaudio include dir: ${AAUDIO_INCLUDE_DIR}")
    message(STATUS "aaudio lib: ${AAUDIO_LIBRARY}")
    set( AAUDIO_INCLUDE_DIRS ${AAUDIO_INCLUDE_DIR} )
    set( AAUDIO_LIBRARIES ${AAUDIO_LIBRARY} )
else()
    set( AAUDIO_INCLUDE_DIRS )
    set( AAUDIO_LIBRARIES )
endif()

mark_as_advanced( AAUDIO_LIBRARIES AAUDIO_INCLUDE_DIRS )
