project(g729)

# Find libbcg729 (no pkg-config, do it manually)
find_path(BCG729_INCLUDE_DIR bcg729/decoder.h)
find_library(BCG729_LIBRARY NAMES bcg729)

if(NOT BCG729_INCLUDE_DIR OR NOT BCG729_LIBRARY)
  message(STATUS "libbcg729 not found - skipping g729 module")
  return()
endif()

# Register module in detected list
list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

# Source files
set(SRCS g729.c)

# Build static or dynamic module
if(STATIC)
  add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
  add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Include + link
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
  ${BCG729_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE
  ${BCG729_LIBRARY})