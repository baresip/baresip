project(avcapture)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS avcapture.m)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
  "-framework Foundation"
  "-framework CoreMedia"
  "-framework CoreVideo"
  "-framework AVFoundation"
)
