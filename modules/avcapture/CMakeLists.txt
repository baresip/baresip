project(avcapture)

set(SRCS avcapture.m)

if(STATIC)
  add_library(${PROJECT_NAME} STATIC ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE baresip
  "-framework Foundation"
  "-framework CoreMedia"
  "-framework CoreVideo"
  "-framework AVFoundation"
)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

list(APPEND MODULES ${PROJECT_NAME})
set(MODULES ${MODULES} PARENT_SCOPE)
