project(mpa)

find_package(MPA)

if(NOT MPA_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS mpa.c decode.c encode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${MPA_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${MPA_LIBRARIES})
