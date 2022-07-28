project(pulse_async)

set(SRCS pulse.c pastream.c player.c recorder.c)

if(STATIC)
    add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${PULSE_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE baresip ${PULSE_LIBRARIES})
