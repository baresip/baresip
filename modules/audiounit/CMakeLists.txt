project(audiounit)

set(SRCS audiounit.c player.c recorder.c sess.c)

if(STATIC)
  add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE baresip
  "-framework CoreAudio"
  "-framework AudioToolbox"
)
