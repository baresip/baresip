project(g722)

if(G722_MODULE_SELECTED AND
  NOT G722_MODULE_SELECTED STREQUAL "${PROJECT_NAME}")
  message(STATUS "${PROJECT_NAME}: skipping, "
    "using ${G722_MODULE_SELECTED} implementation")
    return()
endif()

find_package(SPANDSP)

if(NOT SPANDSP_FOUND)
  return()
endif()

set(G722_MODULE_SELECTED ${PROJECT_NAME}
    CACHE INTERNAL "Selected G722 implementation" FORCE)

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS g722.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${SPANDSP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SPANDSP_LIBRARIES})
