project(alsa)

set(SRCS alsa.c alsa_play.c alsa_src.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${ALSA_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${ALSA_LIBRARIES})
