project(opus)

find_package(OPUS)

if(NOT OPUS_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS opus.c encode.c decode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUS_LIBRARIES})
