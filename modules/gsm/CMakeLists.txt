project(gsm)

find_package(GSM)

if(NOT GSM_FOUND)
  return()
endif()

list(APPEND MODULES_DETECTED ${PROJECT_NAME})
set(MODULES_DETECTED ${MODULES_DETECTED} PARENT_SCOPE)

set(SRCS gsm.c)

if(STATIC)
    add_library(${PROJECT_NAME} OBJECT ${SRCS})
else()
    add_library(${PROJECT_NAME} MODULE ${SRCS})
endif()

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${GSM_INCLUDE_DIRS})
if(GSM_USE_GSM_GSM_H)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_GSM_GSM_H=1)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE ${GSM_LIBRARIES})
