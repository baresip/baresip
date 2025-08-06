project(avcodec)

find_package(FFMPEG COMPONENTS REQUIRED
    avcodec avfilter avformat swscale avdevice avutil)

#if(NOT FFMPEG_FOUND)
#  return()
#endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS avcodec.c encode.c decode.c sdp.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})