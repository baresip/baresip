project(g726)

find_package(SPANDSP)

if(NOT SPANDSP_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS g726.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${SPANDSP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SPANDSP_LIBRARIES})
