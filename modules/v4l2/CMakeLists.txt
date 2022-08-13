project(v4l2)

set(SRCS v4l2.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${V4L2_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${V4L2_LIBRARIES})
