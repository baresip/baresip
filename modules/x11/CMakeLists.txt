project(x11)

set(SRCS x11.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE baresip ${X11_LIBRARIES})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
