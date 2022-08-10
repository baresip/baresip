project(codec2)

set(SRCS codec2.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${CODEC2_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${CODEC2_LIBRARIES})
