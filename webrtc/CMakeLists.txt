#
# CMakeLists.txt
#
# Copyright (C) 2010 - 2022 Alfred E. Heggestad
#

##############################################################################
#
# Versioning
#

project(baresip-webrtc)


##############################################################################
#
# Module/Package Includes
#


##############################################################################
#
# Compile options
#

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

target_link_libraries(${PROJECT_NAME} baresip ${RE_LIBRARIES})
