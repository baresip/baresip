project(gzrtp)

find_package(GZRTP)

if(NOT GZRTP_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS gzrtp.cpp messages.cpp session.cpp srtp.cpp stream.cpp)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${GZRTP_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${GZRTP_LIBRARIES})

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-unknown-pragmas
    -Wno-shadow
    -Wno-ignored-qualifiers
    -Wno-aggregate-return
    -Wno-pedantic
    -Wno-missing-field-initializers
)
