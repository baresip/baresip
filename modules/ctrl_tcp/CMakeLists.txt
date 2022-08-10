project(ctrl_tcp)

set(SRCS ctrl_tcp.c tcp_netstring.c netstring/netstring.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

