project(x11)

find_package(X11)

if(NOT X11_XShm_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS x11.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${X11_LIBRARIES})
