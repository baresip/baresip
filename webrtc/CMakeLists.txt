#
# CMakeLists.txt
#
# Copyright (C) 2010 - 2022 Alfred E. Heggestad
#

##############################################################################
#
# Versioning
#

cmake_minimum_required(VERSION 3.7)

project(baresip-webrtc)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)


##############################################################################
#
# Module/Package Includes
#

find_package(RE REQUIRED)
find_package(BARESIP REQUIRED)


##############################################################################
#
# Compile options
#

set(CMAKE_C_STANDARD 11)

if(MSVC)
  add_compile_options("/W3")
else()
  add_compile_options(-Wall -Wextra)
endif()

include_directories(${RE_INCLUDE_DIRS} ${BARESIP_INCLUDE_DIRS})


##############################################################################
#
# Source/Header section
#

add_executable(${PROJECT_NAME}
  src/demo.c
  src/main.c
  src/sess.c
)

target_link_libraries(${PROJECT_NAME} ${BARESIP_LIBRARIES} ${RE_LIBRARIES})
