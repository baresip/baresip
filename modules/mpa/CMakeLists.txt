project(mpa)

set(SRCS mpa.c decode.c encode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${MPA_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${MPA_LIBRARIES})
