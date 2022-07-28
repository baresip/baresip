project(vp8)

set(SRCS decode.c encode.c sdp.c vp8.c vp8.h)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE baresip ${VPX_LIBRARIES})
