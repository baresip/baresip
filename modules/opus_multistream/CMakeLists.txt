project(opus_multistream)

set(SRCS opus_multistream.c decode.c encode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUS_LIBRARIES})
