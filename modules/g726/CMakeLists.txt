project(g726)

set(SRCS g726.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${SPANDSP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SPANDSP_LIBRARIES})
