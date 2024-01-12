project(netroam)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS netroam.c)

list(APPEND SRCS netlink.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_compile_options(${PROJECT_NAME} PRIVATE -DADD_NETLINK)
