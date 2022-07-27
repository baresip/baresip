project(coreaudio)

set(SRCS coreaudio.c player.c recorder.c)

if(STATIC)
  add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE baresip
  "-framework CoreFoundation"
  "-framework CoreAudio"
  "-framework AudioToolbox"
)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

list(APPEND MODULES ${PROJECT_NAME})
set(MODULES ${MODULES} PARENT_SCOPE)
