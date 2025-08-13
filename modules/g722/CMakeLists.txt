project(g722)

# Try to find SPANDSP first (preferred)
find_package(SPANDSP)

# Try to find libg722 as fallback
find_package(LIBG722)

# Check if at least one library is available
if(NOT SPANDSP_FOUND AND NOT LIBG722_FOUND)
  message(WARNING "g722: neither SPANDSP nor libg722 found - module will not be built")
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS g722.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

# Set preprocessor definitions to indicate which library is being used
# SPANDSP takes precedence if both are available
if(SPANDSP_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_SPANDSP=1)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
        ${SPANDSP_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SPANDSP_LIBRARIES})
    message(STATUS "g722: using SPANDSP library")
elseif(LIBG722_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_LIBG722=1)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
        ${LIBG722_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBG722_LIBRARIES})
    message(STATUS "g722: using libg722 library")
endif()
