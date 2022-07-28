project(amr)

set(SRCS amr.c sdp.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${AMR_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE baresip ${AMR_LIBRARIES})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
