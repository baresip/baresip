project(opus)

# Skip opus module when disabled via Conan
if(BARESIP_USE_CONAN AND DEFINED CONAN_OPUS_ENABLED AND NOT CONAN_OPUS_ENABLED)
    return()
endif()

# Use Conan's Opus package when available
if(BARESIP_USE_CONAN)
    find_package(opus QUIET)
endif()

if(NOT TARGET Opus::opus)
    find_package(OPUS)  # Fallback to system FindOPUS.cmake
    if(NOT OPUS_FOUND)
        return()
    endif()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS opus.c encode.c decode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Link dependencies - handle both Conan and system packages
if(BARESIP_USE_CONAN AND TARGET Opus::opus)
    target_link_libraries(${PROJECT_NAME} PRIVATE Opus::opus)
    # Add to global list for main baresip library linking
    set_property(GLOBAL APPEND PROPERTY BARESIP_LINK_LIBRARIES Opus::opus)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUS_LIBRARIES})
endif()