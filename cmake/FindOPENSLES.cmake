set( TOOLCHAIN "${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}" )

find_path(OPENSLES_INCLUDE_DIR
    NAMES SLES/OpenSLES.h
    PATHS "${TOOLCHAIN}/sysroot/usr/include"
)

# TARGET examples: aarch64-linux-android, arm-linux-androideabi,
# x86_64-linux-android

find_library(OPENSLES_LIBRARY
    NAME OpenSLES
    HINTS "${TOOLCHAIN}/sysroot/usr/lib/${TARGET}/${CMAKE_ANDROID_API}"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OPENSLES DEFAULT_MSG OPENSLES_LIBRARY
    OPENSLES_INCLUDE_DIR)

if(OPENSLES_FOUND)
    message(STATUS "opensles include dir: ${OPENSLES_INCLUDE_DIR}")
    set( OPENSLES_INCLUDE_DIRS ${OPENSLES_INCLUDE_DIR} )
    set( OPENSLES_LIBRARIES ${OPENSLES_LIBRARY} )
else()
    set( OPENSLES_INCLUDE_DIRS )
    set( OPENSLES_LIBRARIES )
endif()

mark_as_advanced( OPENSLES_LIBRARIES OPENSLES_INCLUDE_DIRS )
