project(g7221)

set(SRCS g7221.c encode.c decode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${G7221_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${G7221_LIBRARIES})
