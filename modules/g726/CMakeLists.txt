project(g726)

set(SRCS g726.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${SPANDSP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE baresip ${SPANDSP_LIBRARIES})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

list(APPEND MODULES ${PROJECT_NAME})
set(MODULES ${MODULES} PARENT_SCOPE)
