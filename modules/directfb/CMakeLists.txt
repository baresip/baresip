project(directfb)

set(SRCS directfb.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${DIRECTFB_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${DIRECTFB_LIBRARIES})
