project(evdev)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
else()
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS evdev.c print.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

