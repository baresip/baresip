project(ctrl_tcp)

set(SRCS ctrl_tcp.c tcp_netstring.c netstring/netstring.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
    target_link_libraries(${PROJECT_NAME} PRIVATE baresip)
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

