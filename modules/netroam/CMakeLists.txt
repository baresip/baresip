project(netroam)

include(CheckIncludeFile)

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS netroam.c)
set(MODDEFS "")

check_include_file(linux/netlink.h HAVE_NETLINK_H)
if(HAVE_NETLINK_H)
  list(APPEND MODDEFS ADD_NETLINK)
  list(APPEND SRCS netlink.c)
endif()

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE ${MODDEFS})
