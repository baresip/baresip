project(mixminus)

set(SRCS mixminus.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
