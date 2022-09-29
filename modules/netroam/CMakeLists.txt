project(netroam)

set(SRCS netroam.c)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    list(APPEND SRCS netlink.c)
endif()

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_compile_options(${PROJECT_NAME} PRIVATE -DADD_NETLINK)
endif()
