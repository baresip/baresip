project(vp8)

find_package(VPX)

if(NOT VPX_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS decode.c encode.c sdp.c vp8.c vp8.h)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${VPX_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${VPX_LIBRARIES})
