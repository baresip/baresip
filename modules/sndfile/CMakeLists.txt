project(sndfile)

find_package(SNDFILE)

if(NOT SNDFILE_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS sndfile.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${SNDFILE_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SNDFILE_LIBRARIES})
