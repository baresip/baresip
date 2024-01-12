project(mqtt)

find_package(MOSQUITTO)

if(NOT MOSQUITTO_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS mqtt.c publish.c subscribe.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${MOSQUITTO_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${MOSQUITTO_LIBRARY})
