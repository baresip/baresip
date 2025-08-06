project(sndfile)

# Skip if explicitly disabled via Conan
if(BARESIP_USE_CONAN AND DEFINED CONAN_SNDFILE_ENABLED AND NOT CONAN_SNDFILE_ENABLED)
    return()
endif()

# Try Conan package first, then fallback to system Find module
if(BARESIP_USE_CONAN)
    find_package(SndFile QUIET)
endif()

if(NOT TARGET SndFile::sndfile)
    find_package(SNDFILE)
    if(NOT SNDFILE_FOUND)
        return()
    endif()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS sndfile.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Link dependencies based on whether we're using Conan or not
if(BARESIP_USE_CONAN AND TARGET SndFile::sndfile)
    target_link_libraries(${PROJECT_NAME} PRIVATE SndFile::sndfile)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${SNDFILE_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SNDFILE_LIBRARIES})
endif()
