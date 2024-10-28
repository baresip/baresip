project(webrtc_aecm)

find_package(WEBRTC_AECM)

if(NOT WEBRTC_AECM_FOUND)
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

target_include_directories(${PROJECT_NAME} PRIVATE ${WEBRTC_AECM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${WEBRTC_AECM_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE
  -Wno-missing-field-initializers
)
