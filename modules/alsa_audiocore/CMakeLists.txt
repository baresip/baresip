project(alsa_audiocore)

find_package(ALSA)

if(NOT ALSA_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS alsa_audiocore.c audiocore.c alsa_ac_play.c alsa_ac_src.c alsa_ac.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${ALSA_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${ALSA_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE -D_POSIX_SOURCE)

