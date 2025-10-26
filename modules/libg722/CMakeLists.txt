project(libg722)

find_package(LIBG722)

if(NOT LIBG722_FOUND)
  message(WARNING "libg722: libg722 not found - module will not be built")
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS libg722.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE USE_LIBG722=1)

if(TARGET LIBG722::LIBG722)
    target_link_libraries(${PROJECT_NAME} PRIVATE LIBG722::LIBG722)
else()
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
        ${LIBG722_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBG722_LIBRARIES})
endif()

message(STATUS "libg722: using libg722 library")
