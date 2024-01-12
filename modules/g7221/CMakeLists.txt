project(g7221)

find_package(G7221)

if(NOT G7221_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS g7221.c encode.c decode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${G7221_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${G7221_LIBRARIES})
