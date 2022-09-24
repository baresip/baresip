project(webrtc_aec)

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
target_link_options(${PROJECT_NAME} PRIVATE -lstdc++)
