project(avcodec)

find_package(FFMPEG COMPONENTS
    avcodec avfilter avformat swscale avdevice avutil)

if(NOT FFMPEG_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS avcodec.c encode.c decode.c sdp.c parse/bs.c parse/h264_parse.c parse/h265_parse.c parse/h2645_util.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
