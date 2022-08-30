project(gzrtp)

set(SRCS gzrtp.cpp messages.cpp session.cpp srtp.cpp stream.cpp)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${GZRTP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${GZRTP_LIBRARIES})
