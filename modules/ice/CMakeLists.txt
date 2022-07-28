project(ice)

set(SRCS ice.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
    target_link_libraries(${PROJECT_NAME} PRIVATE baresip)
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
