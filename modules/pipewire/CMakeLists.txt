project(pipewire)

set(SRCS pipewire.c playback.c capture.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${PIPEWIRE_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${PIPEWIRE_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${PIPEWIRE_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE
  -Wno-pedantic
  -D_GNU_SOURCE
)
