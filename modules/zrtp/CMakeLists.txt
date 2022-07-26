project(zrtp)

set(SRCS zrtp.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${ZRTP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE baresip ${ZRTP_LIBRARIES})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

list(APPEND MODULES ${PROJECT_NAME})
set(MODULES ${MODULES} PARENT_SCOPE)
