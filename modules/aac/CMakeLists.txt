project(aac)

set(SRCS aac.c encode.c decode.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${AAC_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${AAC_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE -Wno-unused-function)
