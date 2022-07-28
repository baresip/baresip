project(opus)

set(SRCS opus.c encode.c decode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE baresip ${OPUS_LIBRARIES})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
