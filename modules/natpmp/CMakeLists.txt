project(natpmp)

set(SRCS natpmp.c libnatpmp.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

