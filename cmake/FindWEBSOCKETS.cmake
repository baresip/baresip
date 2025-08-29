# Debug version of FindWEBSOCKETS.cmake
# This will print out what it finds to help debug

message(STATUS "=== WEBSOCKETS Debug ===")

# First try pkg-config
if(NOT WIN32)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        message(STATUS "PkgConfig found, checking for libwebsockets...")
        pkg_check_modules(WEBSOCKETS_PC libwebsockets)
        if(WEBSOCKETS_PC_FOUND)
            message(STATUS "pkg-config found libwebsockets")
            message(STATUS "  Include dirs: '${WEBSOCKETS_PC_INCLUDE_DIRS}'")
            message(STATUS "  Libraries: '${WEBSOCKETS_PC_LIBRARIES}'")
            message(STATUS "  Library dirs: '${WEBSOCKETS_PC_LIBRARY_DIRS}'")
            message(STATUS "  CFlags: '${WEBSOCKETS_PC_CFLAGS}'")

            set(WEBSOCKETS_LIBRARIES ${WEBSOCKETS_PC_LIBRARIES})
            set(WEBSOCKETS_LIBRARY_DIRS ${WEBSOCKETS_PC_LIBRARY_DIRS})
            set(WEBSOCKETS_CFLAGS ${WEBSOCKETS_PC_CFLAGS})

            # If pkg-config provides include dirs, use them
            if(WEBSOCKETS_PC_INCLUDE_DIRS)
                set(WEBSOCKETS_INCLUDE_DIRS ${WEBSOCKETS_PC_INCLUDE_DIRS})
                message(STATUS "Using pkg-config include dirs: '${WEBSOCKETS_INCLUDE_DIRS}'")
            endif()
        else()
            message(STATUS "pkg-config did NOT find libwebsockets")
        endif()
    else()
        message(STATUS "PkgConfig NOT found")
    endif()
endif()

# Always search for include directories
message(STATUS "Searching for include directories...")
message(STATUS "Current WEBSOCKETS_INCLUDE_DIRS: '${WEBSOCKETS_INCLUDE_DIRS}'")

find_path(WEBSOCKETS_INCLUDE_DIRS_SEARCH
    NAMES libwebsockets.h
    PATHS
        /usr/include
        /usr/local/include
        /opt/local/include
        ${CMAKE_INSTALL_PREFIX}/include
    NO_DEFAULT_PATH
)

message(STATUS "First search result: '${WEBSOCKETS_INCLUDE_DIRS_SEARCH}'")

if(NOT WEBSOCKETS_INCLUDE_DIRS_SEARCH)
    message(STATUS "First search failed, trying system paths...")
    find_path(WEBSOCKETS_INCLUDE_DIRS_SEARCH
        NAMES libwebsockets.h
    )
    message(STATUS "System search result: '${WEBSOCKETS_INCLUDE_DIRS_SEARCH}'")
endif()

if(NOT WEBSOCKETS_INCLUDE_DIRS)
    set(WEBSOCKETS_INCLUDE_DIRS ${WEBSOCKETS_INCLUDE_DIRS_SEARCH})
    message(STATUS "Set WEBSOCKETS_INCLUDE_DIRS to: '${WEBSOCKETS_INCLUDE_DIRS}'")
endif()

message(STATUS "Final values:")
message(STATUS "  WEBSOCKETS_INCLUDE_DIRS: '${WEBSOCKETS_INCLUDE_DIRS}'")
message(STATUS "  WEBSOCKETS_LIBRARIES: '${WEBSOCKETS_LIBRARIES}'")
message(STATUS "  WEBSOCKETS_LIBRARY_DIRS: '${WEBSOCKETS_LIBRARY_DIRS}'")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WEBSOCKETS DEFAULT_MSG
    WEBSOCKETS_INCLUDE_DIRS
    WEBSOCKETS_LIBRARIES)

if(WEBSOCKETS_FOUND)
    mark_as_advanced(WEBSOCKETS_INCLUDE_DIRS WEBSOCKETS_LIBRARIES WEBSOCKETS_LIBRARY_DIRS)
endif()