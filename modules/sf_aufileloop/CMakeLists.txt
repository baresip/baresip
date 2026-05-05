project(sf_aufileloop)

find_package(FFMPEG REQUIRED COMPONENTS avformat avcodec avutil swresample)

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT sf_aufileloop.c)
else()
  add_library(${PROJECT_NAME} MODULE sf_aufileloop.c)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
