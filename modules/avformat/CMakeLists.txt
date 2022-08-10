project(avformat)

set(SRCS avformat.c audio.c video.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
