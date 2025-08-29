project(openai_rt)

find_package(CURL REQUIRED)
find_package(WEBSOCKETS REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(SNDFILE REQUIRED)

# Find json-c library
pkg_check_modules(JSON_C REQUIRED json-c)

# Find libre
find_library(RE_LIBRARY NAMES re PATHS /usr/lib /usr/local/lib)

if(NOT RE_LIBRARY)
    message(FATAL_ERROR "libre not found")
endif()

if(NOT SNDFILE_FOUND)
  message(FATAL_ERROR "libsndfile not found")
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

# Source files
set(SRCS
    openai_rt.c
    audio.c
    websocket.c
    utils.c
    calls.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Handle include directories
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${WEBSOCKETS_INCLUDE_DIRS}
    ${JSON_C_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIR})

# Handle library directories (only if they exist)
if(CURL_LIBRARY_DIRS)
    target_link_directories(${PROJECT_NAME} PRIVATE ${CURL_LIBRARY_DIRS})
endif()

if(WEBSOCKETS_LIBRARY_DIRS)
    target_link_directories(${PROJECT_NAME} PRIVATE ${WEBSOCKETS_LIBRARY_DIRS})
endif()

if(JSON_C_LIBRARY_DIRS)
    target_link_directories(${PROJECT_NAME} PRIVATE ${JSON_C_LIBRARY_DIRS})
endif()

# Link libraries - IMPORTANT: Add baresip core libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${RE_LIBRARY}
    ${CURL_LIBRARIES}
    ${WEBSOCKETS_LIBRARIES}
    ${JSON_C_LIBRARIES}
    ${SNDFILE_LIBRARIES})

# Add additional system libraries for MODULE builds
if(NOT STATIC)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ssl
        crypto
        z
        m
        pthread
    )
endif()

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-pedantic
    -D_GNU_SOURCE)