project(webrtc_aecm)

set(SRCS aec.cpp decode.cpp encode.cpp)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${WEBRTC_AECM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${WEBRTC_AECM_LIBRARIES})
