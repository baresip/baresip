project(swscale)

# Skip if explicitly disabled via Conan
if(BARESIP_USE_CONAN AND DEFINED CONAN_FFMPEG_ENABLED AND NOT CONAN_FFMPEG_ENABLED)
    return()
endif()

# Try Conan package first, then fallback to system Find module
if(BARESIP_USE_CONAN)
    find_package(ffmpeg QUIET)
endif()

if(NOT TARGET ffmpeg::ffmpeg)
    find_package(FFMPEG COMPONENTS
        avcodec avfilter avformat swscale swresample avdevice avutil)
    if(NOT FFMPEG_FOUND)
        return()
    endif()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS swscale.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Link dependencies based on whether we're using Conan or not
if(BARESIP_USE_CONAN AND TARGET ffmpeg::ffmpeg)
    target_link_libraries(${PROJECT_NAME} PRIVATE ffmpeg::ffmpeg)
    # Add to global list for main baresip library linking
    set_property(GLOBAL APPEND PROPERTY BARESIP_LINK_LIBRARIES ffmpeg::ffmpeg)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
endif()
