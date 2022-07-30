project(httpd)

set(SRCS httpd.c)

if(STATIC)
  add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()
