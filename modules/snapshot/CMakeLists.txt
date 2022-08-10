project(snapshot)

set(SRCS png_vf.c png_vf.h snapshot.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${PNG_LIBRARIES})
