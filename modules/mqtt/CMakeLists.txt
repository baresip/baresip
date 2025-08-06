project(mqtt)

# Check if MOSQUITTO is disabled in Conan
if(BARESIP_USE_CONAN AND DEFINED CONAN_MOSQUITTO_ENABLED AND NOT CONAN_MOSQUITTO_ENABLED)
    message(STATUS "MOSQUITTO disabled in Conan configuration, skipping mqtt module")
    return()
endif()

# Try Conan first, then fallback to manual find
if(BARESIP_USE_CONAN)
    find_package(mosquitto QUIET)
    if(mosquitto_FOUND)
        set(MOSQUITTO_FOUND TRUE)
    else()
        find_package(MOSQUITTO)
    endif()
else()
    find_package(MOSQUITTO)
endif()

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

# Link dependencies based on whether we're using Conan or not
if(BARESIP_USE_CONAN AND TARGET mosquitto::libmosquitto)
    target_link_libraries(${PROJECT_NAME} PRIVATE mosquitto::libmosquitto)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${MOSQUITTO_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${MOSQUITTO_LIBRARY})
endif()
