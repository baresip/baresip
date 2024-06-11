project(webrtc_aec)

find_package(WEBRTC_AEC)

if(NOT WEBRTC_AEC_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS aec.cpp decode.cpp encode.cpp)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${WEBRTC_AEC_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${WEBRTC_AEC_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE
  -isystem $(WEBRTC_AEC_INCLUDE_DIRS)
  -fPIC
  -Wno-shadow
  -Wno-pedantic
  -Wno-missing-field-initializers
  -Wno-unused-parameter
)
target_link_options(${PROJECT_NAME} PUBLIC -lstdc++)
