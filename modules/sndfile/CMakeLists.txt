project(sndfile)

set(SRCS sndfile.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${SNDFILE_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SNDFILE_LIBRARIES})
