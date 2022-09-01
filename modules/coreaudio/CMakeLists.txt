project(coreaudio)

set(SRCS coreaudio.c player.c recorder.c)

if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
  "-framework CoreFoundation"
  "-framework CoreAudio"
  "-framework AudioToolbox"
)
