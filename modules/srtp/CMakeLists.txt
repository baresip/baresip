project(srtp)

set(SRCS sdes.c sdes.h srtp.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

list(APPEND MODULES ${PROJECT_NAME})
set(MODULES ${MODULES} PARENT_SCOPE)
