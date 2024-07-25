project(audiounit)

if(NOT ${CMAKE_HOST_SYSTEM_NAME} MATCHES "Darwin")
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS audiounit.c player.c recorder.c sess.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
  "-framework CoreAudio"
  "-framework AudioToolbox"
)
